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

#include "conn_spi.h"
 
/* Callback SPI -> bridge: transforme/forward vers send_fn.
 * ATTENTION: le buffer rx fourni par le driver est libéré après le callback;
 * il faut donc copier avant d'appeler send_fn.
 */
// bridge.c
#include "gw_msg.h"     // pour gw_msg_t / gw_payload_t

// Exemple minimal de transform qui fixe le topic en fonction d’une opération SPI
int spi_to_mqtt_default(const gw_msg_t* in, gw_msg_t* out, void* user){
    gw_bridge_runtime_t* rt = (gw_bridge_runtime_t*)user;
    if (!in || !out || !rt) return -1;

    memset(out, 0, sizeof(*out));

    out->protocole = KIND_MQTT;         // destination
    out->pl = in->pl;                    // réutilise le payload tel quel
    out->pl.topic = rt->topic_prefix[0] 
                    ? rt->topic_prefix 
                    : "ingest/spi/read";          

    return 0;
}



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
    case KIND_HTTP_SERVER:
    case KIND_COAP:
    default:
        // leave dest_ctx/send_fn/send_ctx as-is (unsupported will be caught in start)
        break;
    }

    // Allocate/assign SOURCE runtime
    switch (rt->from->kind) {
    case KIND_SPI: {
        spi_runtime_t* spi = (spi_runtime_t*)calloc(1, sizeof(*spi));
        if (!spi) return -1;
        rt->source_ctx = spi;
        break;
    }
    case KIND_MODBUS_RTU:
    case KIND_MODBUS_TCP:
    case KIND_UART:
    case KIND_I2C:
    default:
        // leave source_ctx as-is (unsupported will be caught in start)
        break;
    }

    // Pick a default TRANSFORM for SPI -> MQTT
    if (!rt->transform &&
        rt->from->kind == KIND_SPI &&
        rt->to->kind   == KIND_MQTT)
    {
        rt->transform      = spi_to_mqtt_default; // <-- this wires it
        rt->transform_user = rt;                  // so the transform can read topic_prefix, etc.
    }

    /* Pas de transform par défaut pour SPI:
     * - Soit tu laisses brut (topic "<prefix>/spi/<op>")
     * - Soit tu assignes rt->transform depuis la config/app si besoin
     */

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
    case KIND_HTTP_SERVER:
    case KIND_COAP:

    default:
        fprintf(stderr, "[%s] destination kind=%d not supported yet\n",
                rt->id[0] ? rt->id : "bridge", (int)rt->to->kind);
        return -2;
    }

    /* 2) Start source (no writes to rt) */
    // the kind HTTP_SERVER it is just en example to test the code quickly, it should be in detinations not sources
    switch (rt->from->kind) {
    case KIND_SPI: {
        if (!rt->source_ctx) {
            fprintf(stderr, "[%s] SPI runtime not prepared\n",
                    rt->id[0] ? rt->id : "bridge");
            return -1;
        }
        int rc = spi_open_from_config(&rt->from->u.spi,
                                      (spi_runtime_t*)rt->source_ctx,
                                      on_spi_rx, rt);
        if (rc != 0) {
            fprintf(stderr, "[%s] spi open failed\n", rt->id[0] ? rt->id : "bridge");
            return -1;
        }
        // One initial pass (optional)
        (void)spi_run_transactions((spi_runtime_t*)rt->source_ctx);

        // Start periodic polling (1000 ms or read from config if you added poll_ms)
        rc = spi_start_polling((spi_runtime_t*)rt->source_ctx, /*poll_ms=*/1000);
        if (rc != 0) { fprintf(stderr, "[%s] spi_start_polling failed\n",
                                rt->id[0] ? rt->id : "bridge"); return -1; }

        //printf("[bridge:%s] SPI(%s) → %s(%s) [prefix=%s] [poll=1000ms]\n", "");
        return 0;
    }

    case KIND_MODBUS_RTU:
    case KIND_MODBUS_TCP:
    case KIND_UART:
    case KIND_I2C:


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
        case KIND_SPI:
            if (rt->source_ctx) {
                spi_stop_polling((spi_runtime_t*)rt->source_ctx);
                spi_close((spi_runtime_t*)rt->source_ctx);
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