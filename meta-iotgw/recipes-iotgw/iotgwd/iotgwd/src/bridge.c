/**
 * @file bridge.c
 * @brief Implémentation d’un bridge générique (source → destination).
 *        Implémentation courante : HTTP(server) → MQTT.
 *
 * NOTE: Toute la logique de couplage (qui appelle quoi) est ici.
 *       Le main ne dépend plus de conn_mqtt.h ni de http_server_bridge.h.
 */


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bridge.h"
#include "config_types.h"
#include"config_loader.h"

#include "conn_http_server.h"   // http runtime API + on_http_rx
#include "conn_mqtt.h"          // mqtt_send_adapter + http_to_mqtt_default

/* Fill every field of gw_bridge_runtime_t here. Do NOT start anything. */
int prepare_bridge_runtime_t(const config_t* cfg,
                             const char* topic_prefix,
                             const char* bridge_id,
                             const char* connector_src_name,
                             const char* connector_dst_name,
                             gw_bridge_runtime_t* rt)
{
    if (!cfg || !rt || !connector_src_name || !connector_dst_name) return -1;

    memset(rt, 0, sizeof(*rt));

    // Resolve connectors (by name from YAML)
    rt->from = config_find_connector(cfg, connector_src_name);
    rt->to   = config_find_connector(cfg, connector_dst_name);
    if (!rt->from || !rt->to) {
        fprintf(stderr, "[prepare] missing connector (from:%s to:%s)\n",
                connector_src_name, connector_dst_name);
        return -1;
    }

    // Copy identifiers (safe)
    if (bridge_id && bridge_id[0])
        strncpy(rt->id, bridge_id, sizeof(rt->id)-1);
    strncpy(rt->topic_prefix,
            (topic_prefix && topic_prefix[0]) ? topic_prefix : "ingest",
            sizeof(rt->topic_prefix)-1);

    // Allocate/assign DEST runtime + default sender
    switch (rt->to->kind) {
    case KIND_MQTT: {
        mqtt_runtime_t* mqtt = (mqtt_runtime_t*)calloc(1, sizeof(*mqtt));
        if (!mqtt) return -1;
        rt->dest_ctx = mqtt;

        // Default sender for MQTT
        rt->send_fn  = mqtt_send_adapter;
        rt->send_ctx = mqtt;
        break;
    }
    default:
        // leave dest_ctx/send_fn/send_ctx as-is (unsupported will be caught in start)
        break;
    }

    // Allocate/assign SOURCE runtime
    switch (rt->from->kind) {
    case KIND_HTTP_SERVER: {
        http_server_runtime_t* http = (http_server_runtime_t*)calloc(1, sizeof(*http));
        if (!http) return -1;
        rt->source_ctx = http;
        break;
    }
    default:
        // leave source_ctx as-is (unsupported will be caught in start)
        break;
    }

    // Pick a default TRANSFORM for common pairs (HTTP(server) -> MQTT)
    if (!rt->transform &&
        rt->from->kind == KIND_HTTP_SERVER &&
        rt->to->kind   == KIND_MQTT)
    {
        rt->transform = http_to_mqtt_default; // lives in conn_mqtt.c
        rt->transform_user = rt;              // for access to topic_prefix, etc.
    }

    return 0;
}

/* Start using PREPARED fields only. Do not write to rt. */
int gw_bridge_start(gw_bridge_runtime_t* rt)
{
    if (!rt || !rt->from || !rt->to) return -1;

    /* 1) Start destination (no writes to rt) */
    switch (rt->to->kind) {
    case KIND_MQTT: {
        if (!rt->dest_ctx || !rt->send_fn || !rt->send_ctx) {
            fprintf(stderr, "[%s] MQTT runtime/sender not prepared\n",
                    rt->id[0] ? rt->id : "bridge");
            return -1;
        }
        int rc = mqtt_connect_from_config(&rt->to->u.mqtt,
                                          (mqtt_runtime_t*)rt->dest_ctx,
                                          /*on_mqtt_msg*/NULL, /*user*/NULL);
        if (rc != 0) {
            fprintf(stderr, "[%s] mqtt connect failed\n", rt->id[0] ? rt->id : "bridge");
            return -1;
        }
        break;
    }
    default:
        fprintf(stderr, "[%s] destination kind=%d not supported yet\n",
                rt->id[0] ? rt->id : "bridge", (int)rt->to->kind);
        return -2;
    }

    /* 2) Start source (no writes to rt) */
    switch (rt->from->kind) {
    case KIND_HTTP_SERVER: {
        if (!rt->source_ctx) {
            fprintf(stderr, "[%s] HTTP runtime not prepared\n",
                    rt->id[0] ? rt->id : "bridge");
            return -1;
        }
        int rc = conn_http_server_start_from_config(&rt->from->u.http_server,
                                                    (http_server_runtime_t*)rt->source_ctx);
        if (rc != 0) {
            fprintf(stderr, "[%s] http server start failed\n", rt->id[0] ? rt->id : "bridge");
            return -1;
        }

        // HTTP stays generic: it will call rt->transform + rt->send_fn
        conn_http_server_set_rx_cb((http_server_runtime_t*)rt->source_ctx, on_http_rx, rt);

        printf("[bridge:%s] HTTP(%s) → %s(%s) [prefix=%s]\n",
               rt->id[0] ? rt->id : "<unnamed>",
               rt->from->name,
               (rt->to->kind == KIND_MQTT ? "MQTT" : "DST"),
               rt->to->name,
               rt->topic_prefix[0] ? rt->topic_prefix : "ingest");
        return 0;
    }
    default:
        fprintf(stderr, "[%s] source kind=%d not supported yet\n",
                rt->id[0] ? rt->id : "bridge", (int)rt->from->kind);
        return -2;
    }
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