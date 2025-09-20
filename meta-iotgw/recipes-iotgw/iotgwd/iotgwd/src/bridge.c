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
#include "gw_msg.h"





int gw_bridge_start(const char* bridge_id,
                    const char* topic_prefix,
                    gw_bridge_runtime_t* out)
{
    if (!out) return -1;
    if (!out->from || !out->to) return -1;

    // Preserve from/to + strings, then clear and restore
    const connector_any_t* from = out->from;
    const connector_any_t* to   = out->to;
    char idbuf[128] = {0}, pfx[128] = {0};
    if (bridge_id)   strncpy(idbuf, bridge_id, sizeof(idbuf)-1);
    if (topic_prefix) strncpy(pfx, topic_prefix, sizeof(pfx)-1);

    memset(out, 0, sizeof(*out));
    out->from = from;
    out->to   = to;
    strncpy(out->id, idbuf, sizeof(out->id)-1);
    strncpy(out->topic_prefix, pfx[0] ? pfx : "ingest", sizeof(out->topic_prefix)-1);

    /* 1) Prepare destination */
    switch (out->to->kind) {
    case KIND_MQTT: {
        mqtt_runtime_t* mqtt = (mqtt_runtime_t*)calloc(1, sizeof(*mqtt));
        if (!mqtt) return -1;

        int rc = mqtt_connect_from_config(&out->to->u.mqtt, mqtt, on_mqtt_msg, NULL);
        if (rc != 0) {
            fprintf(stderr, "[%s] mqtt connect failed\n", out->id[0] ? out->id : "bridge");
            free(mqtt);
            return -1;
        }

        // Record destination runtime + default sender
        out->dest_ctx = mqtt;                                       // <-- important
        out->send_fn  = out->send_fn  ? out->send_fn  : mqtt_send_adapter;
        out->send_ctx = out->send_ctx ? out->send_ctx : (void*)mqtt;
        break;
    }
    default:
        fprintf(stderr, "[%s] destination kind=%d not supported yet\n",
                out->id[0] ? out->id : "bridge", (int)out->to->kind);
        return -2;
    }

    /* 2) Start source + callback */
    switch (out->from->kind) {
    case KIND_HTTP_SERVER: {
        http_server_runtime_t* http = (http_server_runtime_t*)calloc(1, sizeof(*http));
        if (!http) {
            // rollback dest
            switch (out->to->kind) {
            case KIND_MQTT:
                if (out->dest_ctx) { mqtt_close((mqtt_runtime_t*)out->dest_ctx); free(out->dest_ctx); out->dest_ctx = NULL; }
                break;
            default: break;
            }
            return -1;
        }

        int rc = conn_http_server_start_from_config(&out->from->u.http_server, http);
        if (rc != 0) {
            fprintf(stderr, "[%s] http server start failed\n", out->id[0] ? out->id : "bridge");
            // rollback dest
            switch (out->to->kind) {
            case KIND_MQTT:
                if (out->dest_ctx) { mqtt_close((mqtt_runtime_t*)out->dest_ctx); free(out->dest_ctx); out->dest_ctx = NULL; }
                break;
            default: break;
            }
            free(http);
            return -1;
        }

        out->source_ctx = http;
        conn_http_server_set_rx_cb(http, on_http_rx, out);

        // Pick a DEFAULT transform only based on destination (HTTP stays generic)
        if (!out->transform) {
            switch (out->to->kind) {
            case KIND_MQTT:
                out->transform = http_to_mqtt_default;   // declared in conn_mqtt.h
                out->transform_user = out;               // gives topic_prefix to transform
                break;
            default:
                // leave NULL => pass-through (sender must accept KIND_HTTP_SERVER)
                break;
            }
        }

        printf("[bridge:%s] HTTP(%s) → %s(%s) [prefix=%s]\n",
               out->id[0] ? out->id : "<unnamed>",
               out->from->name,
               (out->to->kind == KIND_MQTT ? "MQTT" : "DST"),
               out->to->name,
               out->topic_prefix[0] ? out->topic_prefix : "ingest");
        return 0;
    }
    default:
        fprintf(stderr, "[%s] source kind=%d not supported yet\n",
                out->id[0] ? out->id : "bridge", (int)out->from->kind);
        break;
    }

    /* rollback if unsupported pair */
    switch (out->to->kind) {
    case KIND_MQTT:
        if (out->dest_ctx) { mqtt_close((mqtt_runtime_t*)out->dest_ctx); free(out->dest_ctx); out->dest_ctx = NULL; }
        break;
    default: break;
    }
    return -2;
}


int gw_bridge_stop(gw_bridge_runtime_t* rt)
{
    if (!rt) return -1;

    // Stop source
    if (rt->from) {
        switch (rt->from->kind) {
        case KIND_HTTP_SERVER:
            if (rt->source_ctx) {
                conn_http_server_stop((http_server_runtime_t*)rt->source_ctx);
                free(rt->source_ctx);
                rt->source_ctx = NULL;
            }
            break;
        default: break;
        }
    }

    // Stop destination
    if (rt->to) {
        switch (rt->to->kind) {
        case KIND_MQTT:
            if (rt->dest_ctx) {
                mqtt_close((mqtt_runtime_t*)rt->dest_ctx);
                free(rt->dest_ctx);
                rt->dest_ctx = NULL;
            }
            break;
        default: break;
        }
    }

    return 0;
}