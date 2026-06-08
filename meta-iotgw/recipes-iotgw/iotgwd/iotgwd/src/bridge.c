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
#include "conn_uart.h"
#include "conn_modbus.h"
#include "conn_i2c.h"
 
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

static int uart_to_mqtt_default(const gw_msg_t *in, gw_msg_t *out, void *user) {
    gw_bridge_runtime_t *rt = user;
    if (!in || !out || !rt || in->protocole != KIND_UART) return -1;
    memset(out, 0, sizeof(*out));
    out->protocole = KIND_MQTT;
    out->pl = in->pl;
    out->pl.topic = rt->topic_prefix[0] ? rt->topic_prefix : "ingest/uart/read";
    return 0;
}

static void on_uart_rx(const uint8_t *data, size_t len, void *user) {
    gw_bridge_runtime_t *rt = user;
    if (!rt || !data || len == 0 || !rt->send_fn) return;
    gw_msg_t input = {0};
    gw_msg_t output = {0};
    input.protocole = KIND_UART;
    input.pl.data = data;
    input.pl.len = len;
    input.pl.content_type = "application/octet-stream";
    if (rt->transform && rt->transform(&input, &output, rt->transform_user) == 0)
        (void)rt->send_fn(rt->send_ctx, &output);
}

static int modbus_to_mqtt_default(const gw_msg_t *in, gw_msg_t *out, void *user) {
    gw_bridge_runtime_t *rt = user;
    if (!in || !out || !rt ||
        (in->protocole != KIND_MODBUS_RTU && in->protocole != KIND_MODBUS_TCP))
        return -1;
    memset(out, 0, sizeof(*out));
    out->protocole = KIND_MQTT;
    out->pl = in->pl;
    out->pl.topic = rt->topic_prefix[0] ? rt->topic_prefix : "ingest/modbus/read";
    return 0;
}

static void on_modbus_rx(const uint8_t *data, size_t len, void *user) {
    gw_bridge_runtime_t *rt = user;
    if (!rt || !data || len == 0 || !rt->send_fn || !rt->from) return;
    gw_msg_t input = {0};
    gw_msg_t output = {0};
    input.protocole = rt->from->kind;
    input.pl.data = data;
    input.pl.len = len;
    input.pl.is_text = 1;
    input.pl.content_type = "application/json";
    if (rt->transform && rt->transform(&input, &output, rt->transform_user) == 0)
        (void)rt->send_fn(rt->send_ctx, &output);
}

static int i2c_to_mqtt_default(const gw_msg_t *in, gw_msg_t *out, void *user) {
    gw_bridge_runtime_t *rt = user;
    if (!in || !out || !rt || in->protocole != KIND_I2C) return -1;
    memset(out, 0, sizeof(*out));
    out->protocole = KIND_MQTT;
    out->pl = in->pl;
    out->pl.topic = rt->topic_prefix[0] ? rt->topic_prefix : "ingest/i2c/read";
    return 0;
}

static void on_i2c_rx(const uint8_t *data, size_t len, void *user) {
    gw_bridge_runtime_t *rt = user;
    if (!rt || !data || !len || !rt->send_fn) return;
    gw_msg_t input = {0}, output = {0};
    input.protocole = KIND_I2C;
    input.pl.data = data;
    input.pl.len = len;
    input.pl.is_text = 1;
    input.pl.content_type = "application/json";
    if (rt->transform && rt->transform(&input, &output, rt->transform_user) == 0)
        (void)rt->send_fn(rt->send_ctx, &output);
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
    const bridge_t *bridge_cfg = NULL;
    for (size_t i = 0; bridge_id && i < cfg->bridges.count; ++i) {
        if (cfg->bridges.items[i].name &&
            strcmp(cfg->bridges.items[i].name, bridge_id) == 0) {
            bridge_cfg = &cfg->bridges.items[i];
            break;
        }
    }
    const char *configured_topic = bridge_cfg && bridge_cfg->mapping.topic
                                 ? bridge_cfg->mapping.topic : topic_prefix;
    strncpy(rt->topic_prefix,
            (configured_topic && configured_topic[0]) ? configured_topic : "ingest",
            sizeof(rt->topic_prefix)-1);
    rt->queue_capacity = bridge_cfg && bridge_cfg->buffer.has_size
                       ? (size_t)bridge_cfg->buffer.size : 256;
    rt->queue_policy = bridge_cfg && bridge_cfg->buffer.has_policy &&
                       bridge_cfg->buffer.policy == BUF_DROP_NEW
                       ? MSG_QUEUE_DROP_NEW : MSG_QUEUE_DROP_OLDEST;

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
    case KIND_UART: {
        uart_runtime_t* uart = (uart_runtime_t*)calloc(1, sizeof(*uart));
        if (!uart) return -1;
        uart->fd = -1;
        rt->source_ctx = uart;
        break;
    }
    case KIND_MODBUS_RTU:
    case KIND_MODBUS_TCP: {
        modbus_runtime_t *modbus = calloc(1, sizeof(*modbus));
        if (!modbus) return -1;
        rt->source_ctx = modbus;
        break;
    }
    case KIND_I2C: {
        i2c_runtime_t *i2c = calloc(1, sizeof(*i2c));
        if (!i2c) return -1;
        rt->source_ctx = i2c;
        break;
    }
    default:
        // leave source_ctx as-is (unsupported will be caught in start)
        break;
    }

    if (!rt->transform && rt->from->kind == KIND_UART && rt->to->kind == KIND_MQTT) {
        rt->transform = uart_to_mqtt_default;
        rt->transform_user = rt;
    }

    if (!rt->transform &&
        (rt->from->kind == KIND_MODBUS_RTU || rt->from->kind == KIND_MODBUS_TCP) &&
        rt->to->kind == KIND_MQTT) {
        rt->transform = modbus_to_mqtt_default;
        rt->transform_user = rt;
    }

    if (!rt->transform && rt->from->kind == KIND_I2C && rt->to->kind == KIND_MQTT) {
        rt->transform = i2c_to_mqtt_default;
        rt->transform_user = rt;
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
                                          /*on_mqtt_msg*/NULL, /*user*/NULL,
                                          rt->queue_capacity, rt->queue_policy);
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

    case KIND_UART: {
        if (!rt->source_ctx) return -1;
        int rc = uart_start_from_config(&rt->from->u.uart,
                                        (uart_runtime_t*)rt->source_ctx,
                                        on_uart_rx, rt);
        if (rc != UART_OK) {
            fprintf(stderr, "[%s] uart start failed rc=%d\n",
                    rt->id[0] ? rt->id : "bridge", rc);
            return -1;
        }
        return 0;
    }

    case KIND_MODBUS_RTU:
    case KIND_MODBUS_TCP:
        if (!rt->source_ctx) return -1;
        if (modbus_start_from_config(rt->from,
                                     (modbus_runtime_t*)rt->source_ctx,
                                     on_modbus_rx, rt) != 0) {
            fprintf(stderr, "[%s] modbus polling start failed\n",
                    rt->id[0] ? rt->id : "bridge");
            return -1;
        }
        return 0;

    case KIND_I2C:
        if (!rt->source_ctx) return -1;
        if (i2c_start_from_config(&rt->from->u.i2c,
                                  (i2c_runtime_t*)rt->source_ctx,
                                  on_i2c_rx, rt) != 0) {
            fprintf(stderr, "[%s] i2c polling start failed\n",
                    rt->id[0] ? rt->id : "bridge");
            return -1;
        }
        return 0;

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
        case KIND_UART:
            if (rt->source_ctx) {
                uart_stop((uart_runtime_t*)rt->source_ctx);
                free(rt->source_ctx);
                rt->source_ctx = NULL;
            }
            break;
        case KIND_MODBUS_RTU:
        case KIND_MODBUS_TCP:
            if (rt->source_ctx) {
                modbus_stop((modbus_runtime_t*)rt->source_ctx);
                free(rt->source_ctx);
                rt->source_ctx = NULL;
            }
            break;
        case KIND_I2C:
            if (rt->source_ctx) {
                i2c_stop((i2c_runtime_t*)rt->source_ctx);
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
