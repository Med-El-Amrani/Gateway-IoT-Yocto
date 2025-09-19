// src/print_config.c
#include <stdio.h>
#include <string.h>
#include "print_config.h"

static const char* kind_str(connector_kind_t k) {
    switch (k) {
    case CONN_KIND_MQTT:        return "mqtt";
    case CONN_KIND_MODBUS_RTU:  return "modbus-rtu";
    case CONN_KIND_MODBUS_TCP:  return "modbus-tcp";
    case CONN_KIND_SOCKETCAN:   return "socketcan";
    case CONN_KIND_OPCUA:       return "opcua";
    case CONN_KIND_HTTP_SERVER: return "http-server";
    case CONN_KIND_COAP:        return "coap";
    case CONN_KIND_BLE:         return "ble";
    case CONN_KIND_LORAWAN:     return "lorawan";
    case CONN_KIND_I2C:         return "i2c";
    case CONN_KIND_SPI:         return "spi";
    case CONN_KIND_UART:        return "uart";
    case CONN_KIND_ONEWIRE:     return "onewire";
    case CONN_KIND_ZIGBEE:      return "zigbee";
    default:                    return "unknown";
    }
}

void print_connector_detail(const connector_any_t* c) {
    printf("    - name: %s\n", c->name);
    printf("      type: %s\n", kind_str(c->kind));
    if (c->tags_count) {
        printf("      tags: [");
        for (size_t i=0;i<c->tags_count;i++) {
            printf("%s%s", c->tags[i], (i+1<c->tags_count)?", ":"");
        }
        printf("]\n");
    }
    /* For typed connectors, print a key field or two (non-exhaustive) */
    switch (c->kind) {
    case CONN_KIND_MQTT:
        printf("      client_id: %s\n", c->u.mqtt.params.client_id ? c->u.mqtt.params.client_id : "(null)");
        if (c->u.mqtt.params.url)  printf("      url: %s\n", c->u.mqtt.params.url);
        if (c->u.mqtt.params.host) printf("      host: %s\n", c->u.mqtt.params.host);
        break;
    case CONN_KIND_HTTP_SERVER:
        printf("      bind: %s\n", c->u.http_server.params.bind ? c->u.http_server.params.bind : "(null)");
        printf("      routes: %zu\n", c->u.http_server.params.routes_count);
        break;
    case CONN_KIND_MODBUS_RTU:
        printf("      port: %s\n", c->u.modbus_rtu.params.port ? c->u.modbus_rtu.params.port : "(null)");
        printf("      slaves: %zu\n", c->u.modbus_rtu.params.slaves_count);
        break;
    case CONN_KIND_MODBUS_TCP:
        printf("      host: %s\n", c->u.modbus_tcp.params.host ? c->u.modbus_tcp.params.host : "(null)");
        printf("      map: %zu\n", c->u.modbus_tcp.params.map_count);
        break;
    case CONN_KIND_UART:
        printf("      port: %s\n", c->u.uart.params.port ? c->u.uart.params.port : "(null)");
        printf("      baudrate: %d\n", c->u.uart.params.baudrate);
        break;
    case CONN_KIND_SPI:
    case CONN_KIND_I2C:
    case CONN_KIND_BLE:
    case CONN_KIND_COAP:
    case CONN_KIND_LORAWAN:
    case CONN_KIND_ONEWIRE:
    case CONN_KIND_OPCUA:
    case CONN_KIND_SOCKETCAN:
    case CONN_KIND_ZIGBEE:
        /* If not yet parsed to typed structs, we fall back to opaque */
        if (c->u.opaque.json_params) {
            printf("      (opaque params JSON) %s\n", c->u.opaque.json_params);
        } else {
            printf("      (no params printer implemented yet)\n");
        }
        break;
    default:
        if (c->u.opaque.json_params) {
            printf("      (opaque params JSON) %s\n", c->u.opaque.json_params);
        } else {
            printf("      (unknown type without params)\n");
        }
    }
}

void print_config_summary(const config_t* cfg) {
    printf("== iotgwd config ==\n");
    if (cfg->version_set) printf("version: %.3f\n", cfg->version);
    printf("gateway:\n");
    printf("  name: %s\n", cfg->gateway.name ? cfg->gateway.name : "(null)");
    if (cfg->gateway.timezone) printf("  timezone: %s\n", cfg->gateway.timezone);
    if (cfg->gateway.loglevel) printf("  loglevel: %s\n", cfg->gateway.loglevel);
    if (cfg->gateway.logfile)  printf("  logfile: %s\n",  cfg->gateway.logfile);
    if (cfg->gateway.metrics_port_set) printf("  metrics_port: %d\n", cfg->gateway.metrics_port);

    printf("\nincludes: %zu\n", cfg->includes.count);
    for (size_t i=0;i<cfg->includes.count;i++)
        printf("  - %s\n", cfg->includes.paths[i]);

    printf("\nconnectors: %zu\n", cfg->connectors.count);
    for (size_t i=0;i<cfg->connectors.count;i++)
        print_connector_detail(&cfg->connectors.items[i]);

    printf("\nbridges: %zu\n", cfg->bridges.count);
    for (size_t i=0;i<cfg->bridges.count;i++){
        const bridge_t* b = &cfg->bridges.items[i];
        printf("  - %s: from=%s to=%s\n", b->name, b->from, b->to);
        if (b->mapping.topic)   printf("      mapping.topic: %s\n", b->mapping.topic);
        printf("      mapping.format: %s\n",
               (b->mapping.format==MAP_FMT_JSON)?"json":
               (b->mapping.format==MAP_FMT_KV)?"kv":"raw");
        if (b->mapping.fields_count) {
            printf("      mapping.fields: [");
            for (size_t j=0;j<b->mapping.fields_count;j++)
                printf("%s%s", b->mapping.fields[j], (j+1<b->mapping.fields_count)?", ":"");
            printf("]\n");
        }
        if (b->mapping.timestamp_set)
            printf("      mapping.timestamp: %s\n", b->mapping.timestamp ? "true" : "false");
        if (b->transform_count) {
            printf("      transform: [");
            for (size_t j=0;j<b->transform_count;j++)
                printf("%s%s", b->transform[j], (j+1<b->transform_count)?", ":"");
            printf("]\n");
        }
        if (b->rate_limit.has_max_msgs_per_sec)
            printf("      rate_limit.max_msgs_per_sec: %.3f\n", b->rate_limit.max_msgs_per_sec);
        if (b->rate_limit.has_burst)
            printf("      rate_limit.burst: %d\n", b->rate_limit.burst);
        if (b->buffer.has_size || b->buffer.has_policy) {
            printf("      buffer.size: %s%d\n",
                   b->buffer.has_size?"":"(unset) ", b->buffer.has_size?b->buffer.size:0);
            if (b->buffer.has_policy)
                printf("      buffer.policy: %s\n", b->buffer.policy==0?"drop_oldest":"drop_new");
        }
    }
}
