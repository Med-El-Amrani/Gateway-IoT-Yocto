#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <mosquitto.h>
#include "conn_mqtt.h"

// --- ajoute ceci en tête de ../src/conn_mqtt.c ---
#include <string.h>
#include <stdlib.h>

static char* xstrdup(const char* s){ if(!s) return NULL; size_t n=strlen(s); char* p=malloc(n+1); if(p){ memcpy(p,s,n+1);} return p; }
static char* xstrndup(const char* s, size_t n){ char* p=malloc(n+1); if(p){ memcpy(p,s,n); p[n]='\0'; } return p; }
#define strdup  xstrdup
#define strndup xstrndup

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
        *port = 1883;
    }
    return 0;
}

/* glue pour callbacks */
typedef struct {
    mqtt_msg_cb on_msg;
    void* user;
    mqtt_runtime_t* rt;
} cb_glue_t;

static void on_connect(struct mosquitto* m, void* ud, int rc){
    (void)m;
    cb_glue_t* g = (cb_glue_t*)ud;
    if(rc==0) g->rt->connected = 1;
}

static void on_message(struct mosquitto* m, void* ud, const struct mosquitto_message* msg){
    (void)m;
    cb_glue_t* g = (cb_glue_t*)ud;
    if(g->on_msg) g->on_msg(msg->topic, msg->payload, msg->payloadlen, g->user);
}

int mqtt_connect_from_config(const mqtt_connector_t* cfg,
                             mqtt_runtime_t* rt,
                             mqtt_msg_cb on_msg,
                             void* user)
{
    if(!cfg || !rt) return -1;
    memset(rt, 0, sizeof(*rt));
    mosquitto_lib_init();

    const char* client_id = cfg->params.client_id ? cfg->params.client_id : "iotgw";
    rt->mosq = mosquitto_new(client_id, cfg->params.clean_session_set ? cfg->params.clean_session : true, NULL);
    if(!rt->mosq) return -1;

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
    static cb_glue_t glue;
    glue.on_msg = on_msg;
    glue.user   = user;
    glue.rt     = rt;
    mosquitto_connect_callback_set(rt->mosq, on_connect);
    mosquitto_message_callback_set(rt->mosq, on_message);
    mosquitto_user_data_set(rt->mosq, &glue);

    /* Host/port */
    char *scheme=NULL,*host=NULL;
    int port = 0;
    if(cfg->params.url){
        parse_mqtt_url(cfg->params.url, &scheme, &host, &port);
        if(port==0) port = (!scheme || strcmp(scheme,"mqtt")==0) ? 1883 : 8883;
    }else{
        host = cfg->params.host ? strdup(cfg->params.host) : strdup("localhost");
        port = cfg->params.port ? cfg->params.port : 1883;
    }

    int keepalive = cfg->params.keepalive_set ? cfg->params.keepalive_s : 60;
    int rc = mosquitto_connect(rt->mosq, host, port, keepalive);
    free(scheme); free(host);
    if(rc != MOSQ_ERR_SUCCESS){
        fprintf(stderr, "mosquitto_connect rc=%d\n", rc);
        mosquitto_destroy(rt->mosq); rt->mosq=NULL;
        mosquitto_lib_cleanup();
        return -1;
    }

    /* Souscriptions */
    for(size_t i=0;i<cfg->params.topics_count;i++){
        const char* t = cfg->params.topics[i].topic;
        int qos = cfg->params.topics[i].qos_set ? cfg->params.topics[i].qos : (cfg->params.qos_set ? cfg->params.qos : 0);
        if(t && *t){
            int rc2 = mosquitto_subscribe(rt->mosq, NULL, t, qos);
            if(rc2 != MOSQ_ERR_SUCCESS) fprintf(stderr, "subscribe '%s' rc=%d\n", t, rc2);
        }
    }

    /* Thread loop */
    rc = mosquitto_loop_start(rt->mosq);
    if(rc != MOSQ_ERR_SUCCESS){
        fprintf(stderr, "loop_start rc=%d\n", rc);
        mosquitto_disconnect(rt->mosq);
        mosquitto_destroy(rt->mosq); rt->mosq=NULL;
        mosquitto_lib_cleanup();
        return -1;
    }
    return 0;
}

int mqtt_publish_text(mqtt_runtime_t* rt,
                      const char* topic,
                      const char* payload,
                      int qos,
                      bool retain)
{
    if(!rt || !rt->mosq || !topic) return -1;
    if(qos < 0)      qos = 0;
    else if(qos > 2) qos = 2;
    int rc = mosquitto_publish(rt->mosq, NULL, topic,
                               payload? (int)strlen(payload):0,
                               payload? payload: "",
                               qos, retain);
    return rc==MOSQ_ERR_SUCCESS ? 0 : -1;
}

void mqtt_close(mqtt_runtime_t* rt){
    if(!rt || !rt->mosq) return;
    mosquitto_loop_stop(rt->mosq, true);
    mosquitto_disconnect(rt->mosq);
    mosquitto_destroy(rt->mosq);
    mosquitto_lib_cleanup();
    rt->mosq = NULL;
}
