/**
 * @file bridge.c
 * @brief Implémentation d’un bridge générique (source → destination).
 *        Implémentation courante : HTTP(server) → MQTT.
 *
 * NOTE: Toute la logique de couplage (qui appelle quoi) est ici.
 *       Le main ne dépend plus de conn_mqtt.h ni de http_server_bridge.h.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bridge.h"
#include "conn_mqtt.h"           // mqtt_connect_from_config, mqtt_publish_text
#include "conn_http_server.h"    // notre nouveau connecteur HTTP RX

/* Callback de debug pour MQTT RX (si souscriptions un jour) */
static void on_mqtt_msg(const char* topic, const void* payload, int len, void* user){
    (void)user;
    printf("[MQTT RX] %s | %.*s\n", topic, len, (const char*)payload);
}

#include "gw_msg.h"

/* Adaptateur d'envoi vers MQTT (wrap mqtt_publish_text) */
static int mqtt_send_adapter(const gw_msg_t* out, void* ctx) {
    mqtt_runtime_t* rt = (mqtt_runtime_t*)ctx;
    if (!out || !rt) return -1;
    if (out->protocole != KIND_MQTT) return -1;

    const char* payload = (const char*)(out->pl.data ? out->pl.data : (const uint8_t*)"");
    const char* topic   = out->params.mqtt.client_id ? out->params.mqtt.client_id : "default";

    int rc = mqtt_publish_text(rt, topic, payload,
                               /*qos*/1, /*retain*/0);
    return rc == 0 ? 0 : -1;
}


/* Transform par défaut HTTP -> MQTT (utilise topic_prefix) */
static int http_to_mqtt_default(const gw_msg_t* in, gw_msg_t* out, void* user) {
    gw_bridge_runtime_t* b = (gw_bridge_runtime_t*)user;
    if (!in || !out || !b) return -1;
    if (in->protocole != KIND_HTTP) return -1;

    const char* prefix = b->topic_prefix[0] ? b->topic_prefix : "ingest";

    static char topic[512];
    snprintf(topic, sizeof(topic), "%s", prefix);

    memset(out, 0, sizeof(*out));
    out->protocole = KIND_MQTT;
    out->params.mqtt.client_id = topic;   // hack: stocke le topic ici
    out->pl = in->pl;
    if (!out->pl.content_type)
        out->pl.content_type = in->pl.is_text ? "text/plain" : "application/octet-stream";

    return 0;
}


/* Callback HTTP (générique) : normalise l'entrée puis transform + send */
static int on_http_rx(const char* url, const void* body, size_t len, void* user) {
    gw_bridge_runtime_t* b = (gw_bridge_runtime_t*)user;
    if (!b) return -1;

    // Construire un gw_msg_t "in" depuis la requête HTTP
    gw_msg_t in = {0};
    in.protocole = KIND_HTTP;
    in.params.http_server.bind = (char*)(url ? url : "");
    in.pl.data = (const uint8_t*)(body ? body : (const void*)"");
    in.pl.len  = len;
    in.pl.is_text = 1;

    // Transformer (HTTP -> MQTT par défaut)
    gw_msg_t out = {0};
    gw_transform_fn tf = b->transform ? b->transform : http_to_mqtt_default;
    if (tf(&in, &out, b->transform ? b->transform_user : (void*)b) != 0) {
        fprintf(stderr, "[bridge:%s] transform failed\n", b->id);
        return -1;
    }

    // Envoyer via l’adaptateur MQTT par défaut
    gw_send_fn sendf = b->send_fn ? b->send_fn : mqtt_send_adapter;
    void*      sctx  = b->send_fn ? b->send_ctx : (void*)&b->mqtt_rt;
    if (sendf(&out, sctx) != 0) {
        fprintf(stderr, "[bridge:%s] send failed\n", b->id);
        return -1;
    }
    return 0;
}



int gw_bridge_start(const connector_any_t* from,
                    const connector_any_t* to,
                    const char* bridge_id,
                    const char* topic_prefix,
                    gw_bridge_runtime_t* out)
{
    if(!from || !to || !out) return -1;

    memset(out, 0, sizeof(*out));
    if(bridge_id) strncpy(out->id, bridge_id, sizeof(out->id)-1);
    out->from = from;
    out->to   = to;
    if(topic_prefix && *topic_prefix){
        strncpy(out->topic_prefix, topic_prefix, sizeof(out->topic_prefix)-1);
    }

    /* 1) Préparer la destination */
    switch (to->kind) {
    case CONN_KIND_MQTT: {
        int rc = mqtt_connect_from_config(&to->u.mqtt, &out->mqtt_rt, on_mqtt_msg, NULL);
        if(rc != 0){
            fprintf(stderr, "[%s] mqtt connect failed\n", out->id[0]? out->id:"bridge");
            return -1;
        }
        break;
    }
    default:
        fprintf(stderr, "[%s] destination kind=%d not supported yet\n",
                out->id[0]? out->id:"bridge", (int)to->kind);
        return -2;
    }

    /* 2) Démarrer la source et câbler la callback */
    switch (from->kind) {
    case CONN_KIND_HTTP_SERVER: {
        int rc = conn_http_server_start_from_config(&from->u.http_server, &out->http_rt);
        if(rc != 0){
            fprintf(stderr, "[%s] http server start failed\n", out->id[0]? out->id:"bridge");
            mqtt_close(&out->mqtt_rt);
            return -1;
        }
        conn_http_server_set_rx_cb(&out->http_rt, on_http_rx, out);
        printf("[bridge:%s] HTTP(%s) → MQTT(%s) [prefix=%s]\n",
               out->id[0]? out->id:"<unnamed>",
               from->name, to->name,
               out->topic_prefix[0] ? out->topic_prefix : "ingest");
        return 0;
    }
    default:
        fprintf(stderr, "[%s] source kind=%d not supported yet\n",
                out->id[0]? out->id:"bridge", (int)from->kind);
        break;
    }

    /* rollback si couple non supporté */
    mqtt_close(&out->mqtt_rt);
    return -2;
}

void gw_bridge_stop(gw_bridge_runtime_t* b){
    if(!b) return;
    if (b->from && b->from->kind == CONN_KIND_HTTP_SERVER) {
        conn_http_server_stop(&b->http_rt);
    }
    if (b->to && b->to->kind == CONN_KIND_MQTT) {
        mqtt_close(&b->mqtt_rt);
    }
}
