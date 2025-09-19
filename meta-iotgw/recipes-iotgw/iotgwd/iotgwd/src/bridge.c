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

/* Callback HTTP → publie sur MQTT. Retourne 0 si OK, sinon !=0. */
static int on_http_rx(const char* url, const void* body, size_t len, void* user){
    (void)len;
    gw_bridge_runtime_t* b = (gw_bridge_runtime_t*)user;
    if(!b) return -1;

    /* Construire le topic: "<prefix>/<url_sans_slash_initial>" */
    char topic[512];
    const char* path = (url && url[0]=='/') ? url+1 : (url?url:"");
    const char* prefix = (b->topic_prefix[0] ? b->topic_prefix : "ingest");

    if(path[0])
        snprintf(topic, sizeof(topic), "%s/%s", prefix, path);
    else
        snprintf(topic, sizeof(topic), "%s", prefix);

    /* Le corps HTTP est déjà NUL-terminé côté connecteur ; on peut publier en "text" */
    const char* payload = body ? (const char*)body : "";
    int rc = mqtt_publish_text(&b->mqtt_rt, topic, payload, /*qos*/0, /*retain*/0);
    if(rc != 0){
        fprintf(stderr, "[bridge:%s] mqtt_publish failed topic=%s\n", b->id, topic);
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
