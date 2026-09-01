#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <mosquitto.h>
#include "conn_mqtt.h"
#include "log.h"

// --- ajoute ceci en tête de ../src/conn_mqtt.c ---
#include <string.h>
#include <stdlib.h>

static char* xstrdup(const char* s){ if(!s) return NULL; size_t n=strlen(s); char* p=malloc(n+1); if(p){ memcpy(p,s,n+1);} return p; }
static char* xstrndup(const char* s, size_t n){ char* p=malloc(n+1); if(p){ memcpy(p,s,n); p[n]='\0'; } return p; }
#define strdup  xstrdup
#define strndup xstrndup

static pthread_once_t mosquitto_once = PTHREAD_ONCE_INIT;
static void init_mosquitto_library(void) { (void)mosquitto_lib_init(); }
static int mqtt_publish_direct(void *ctx, const gw_msg_t *msg);

/* --- petits helpers --- */
static int parse_mqtt_url(const char* url, char** scheme, char** host, int* port){
    /* Supporte mqtt://host:port et mqtts://host:port */
    if(!url) return -1;
    const char* p = strstr(url, "://");
    if(!p) return -1;
    *scheme = strndup(url, (size_t)(p - url));
    const char* h = p + 3;
    const char* colon = strrchr(h, ':');
    if(colon){
        *host = strndup(h, (size_t)(colon - h));
        *port = atoi(colon+1);
    }else{
        *host = strdup(h);
        *port = 0;
    }
    return 0;
}

static void on_connect(struct mosquitto* m, void* ud, int rc){
    mqtt_runtime_t* rt = (mqtt_runtime_t*)ud;
    if (atomic_load(&rt->closing)) return;
    atomic_store(&rt->connected, rc == 0);
    if (rc != 0) return;

    const mqtt_params_t *params = &rt->cfg->params;
    for(size_t i = 0; i < params->topics_count; i++) {
        const char* topic = params->topics[i].topic;
        int qos = params->topics[i].qos_set ? params->topics[i].qos
                                            : (params->qos_set ? params->qos : 0);
        if(topic && *topic) (void)mosquitto_subscribe(m, NULL, topic, qos);
    }

    while (atomic_load(&rt->connected) &&
           msg_queue_flush_one(&rt->outbound,
                               mqtt_publish_direct, rt) > 0) {
    }
}

static void on_disconnect(struct mosquitto* m, void* ud, int rc) {
    (void)m; (void)rc;
    mqtt_runtime_t* rt = (mqtt_runtime_t*)ud;
    atomic_store(&rt->connected, 0);
}

static void on_message(struct mosquitto* m, void* ud, const struct mosquitto_message* msg){
    (void)m;
    mqtt_runtime_t* rt = (mqtt_runtime_t*)ud;
    if(rt->on_msg) rt->on_msg(msg->topic, msg->payload, msg->payloadlen,
                              rt->on_msg_user);
}

int mqtt_connect_from_config(const mqtt_connector_t* cfg,
                             mqtt_runtime_t* rt,
                             mqtt_msg_cb on_msg,
                             void* user,
                             size_t queue_capacity,
                             msg_queue_policy_t queue_policy)
{
    if(!cfg || !rt) return -1;
    memset(rt, 0, sizeof(*rt));
    pthread_once(&mosquitto_once, init_mosquitto_library);
    if (msg_queue_init(&rt->outbound, queue_capacity, queue_policy) != 0) return -1;
    rt->cfg = cfg;

    const char* client_id = cfg->params.client_id ? cfg->params.client_id : "iotgw";
    rt->mosq = mosquitto_new(client_id, cfg->params.clean_session_set ? cfg->params.clean_session : true, NULL);
    if(!rt->mosq) { msg_queue_destroy(&rt->outbound); return -1; }

    /* user/pass */
    if(cfg->params.username || cfg->params.password){
        mosquitto_username_pw_set(rt->mosq,
            cfg->params.username ? cfg->params.username : NULL,
            cfg->params.password ? cfg->params.password : NULL);
    }

    /* TLS */
    if(cfg->params.tls.present){
        const char* caf = cfg->params.tls.ca_file;
        const char* crt = cfg->params.tls.cert_file;
        const char* key = cfg->params.tls.key_file;
        if(caf || crt || key){
            int rc = mosquitto_tls_set(rt->mosq, caf, NULL, crt, key, NULL);
            if(rc != MOSQ_ERR_SUCCESS){ fprintf(stderr, "mqtt tls_set=%d\n", rc); }
        }
        if(cfg->params.tls.insecure_skip_verify){
            mosquitto_tls_insecure_set(rt->mosq, true);
        }
    }

    /* Callbacks */
    rt->on_msg = on_msg;
    rt->on_msg_user = user;
    mosquitto_connect_callback_set(rt->mosq, on_connect);
    mosquitto_disconnect_callback_set(rt->mosq, on_disconnect);
    mosquitto_message_callback_set(rt->mosq, on_message);
    mosquitto_user_data_set(rt->mosq, rt);

    /* Host/port */
    char *scheme=NULL,*host=NULL;
    int port = 0;
    if(cfg->params.url){
        if (parse_mqtt_url(cfg->params.url, &scheme, &host, &port) != 0 ||
            !host || !host[0]) {
            free(scheme); free(host);
            mosquitto_destroy(rt->mosq); rt->mosq = NULL;
            msg_queue_destroy(&rt->outbound);
            return -1;
        }
        if(port==0) port = (!scheme || strcmp(scheme,"mqtt")==0) ? 1883 : 8883;
    }else{
        host = cfg->params.host ? strdup(cfg->params.host) : strdup("localhost");
        port = cfg->params.port ? cfg->params.port : 1883;
    }

    int keepalive = cfg->params.keepalive_set ? cfg->params.keepalive_s : 60;
    (void)mosquitto_reconnect_delay_set(rt->mosq, 1, 30, true);
    int rc = mosquitto_connect_async(rt->mosq, host, port, keepalive);
    free(scheme); free(host);
    if(rc != MOSQ_ERR_SUCCESS){
        fprintf(stderr, "mosquitto_connect rc=%d\n", rc);
        mosquitto_destroy(rt->mosq); rt->mosq=NULL;
        msg_queue_destroy(&rt->outbound);
        return -1;
    }

    /* Thread loop */
    rc = mosquitto_loop_start(rt->mosq);
    if(rc != MOSQ_ERR_SUCCESS){
        fprintf(stderr, "loop_start rc=%d\n", rc);
        mosquitto_disconnect(rt->mosq);
        mosquitto_destroy(rt->mosq); rt->mosq=NULL;
        msg_queue_destroy(&rt->outbound);
        return -1;
    }
    return 0;
}
//MODIFIED__
int mqtt_publish_text(mqtt_runtime_t* rt,
                      const char* topic,
                      const char* payload,
                      int qos,
                      bool retain)
{
    if(!rt || !rt->mosq || !topic) return -1;
    if(qos < 0) qos = 0; else if(qos > 2) qos = 2;

    int rc = mosquitto_publish(rt->mosq, NULL, topic,
                               payload ? (int)strlen(payload) : 0,
                               payload ? payload : "",
                               qos, retain);

    if (rc != MOSQ_ERR_SUCCESS) {
        log_err("MQTT publish failed rc=%d (%s) topic=%s",
                rc, mosquitto_strerror(rc), topic);
        return -1;
    }
    return 0;
}


void mqtt_close(mqtt_runtime_t* rt){
    if(!rt) return;
    atomic_store(&rt->closing, 1);
    if(!rt->mosq) { msg_queue_destroy(&rt->outbound); return; }
    mosquitto_loop_stop(rt->mosq, true);
    mosquitto_disconnect(rt->mosq);
    mosquitto_destroy(rt->mosq);
    rt->mosq = NULL;
    atomic_store(&rt->connected, 0);
    msg_queue_destroy(&rt->outbound);
}

static int mqtt_publish_direct(void* ctx, const gw_msg_t* msg)
{
    mqtt_runtime_t* rt = (mqtt_runtime_t*)ctx;
    if (!rt || !rt->mosq || !msg || !atomic_load(&rt->connected)) return -1;
    if (msg->protocole != KIND_MQTT) return -1;

    const char* topic   = (msg->pl.topic && msg->pl.topic[0]) ? msg->pl.topic : "ingest";
    const void* payload = msg->pl.data;
    int         len     = (int)msg->pl.len;
    int         qos     = rt->cfg->params.qos_set ? rt->cfg->params.qos : 1;
    bool        retain  = rt->cfg->params.retain_set ? rt->cfg->params.retain : false;

    int rc = mosquitto_publish(rt->mosq, NULL, topic,
                               payload ? len : 0,
                               payload ? payload : "",
                               qos, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[mqtt] publish FAIL rc=%d (%s) topic=%s len=%d\n",
                rc, mosquitto_strerror(rc), topic, len);
        atomic_fetch_add(&rt->publish_failures, 1);
        return -1;
    }
    atomic_fetch_add(&rt->published, 1);
    return 0;
}

int mqtt_send_adapter(void* ctx, const gw_msg_t* msg)
{
    mqtt_runtime_t* rt = (mqtt_runtime_t*)ctx;
    if (!rt || !msg || msg->protocole != KIND_MQTT) return -1;
    while (atomic_load(&rt->connected)) {
        int flushed = msg_queue_flush_one(&rt->outbound, mqtt_publish_direct, rt);
        if (flushed <= 0) break;
    }
    if (mqtt_publish_direct(rt, msg) == 0) return 0;
    return msg_queue_push(&rt->outbound, msg) == 0 ? 0 : -1;
}

int mqtt_is_connected(const mqtt_runtime_t *rt) {
    return rt ? atomic_load(&rt->connected) : 0;
}

size_t mqtt_queued_messages(mqtt_runtime_t *rt) {
    return rt ? msg_queue_size(&rt->outbound) : 0;
}

uint64_t mqtt_dropped_messages(mqtt_runtime_t *rt) {
    return rt ? msg_queue_dropped(&rt->outbound) : 0;
}

/* Callback de debug pour MQTT RX (si souscriptions un jour) */
void on_mqtt_msg(const char* topic, const void* payload, int len, void* user){
    (void)user;
    printf("[MQTT RX] %s | %.*s\n", topic, len, (const char*)payload);
}
