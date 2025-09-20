// ../src/adapters.c
#include <string.h>
#include <yaml.h>
#include "config_types.h"
#include "connector_registry.h"
#include "params_parsers.h"   // prototypes for per-type parsers

// local helper (same as in loader)
static yaml_node_t* ymap_get(yaml_document_t* doc, yaml_node_t* map, const char* key){
    if(!map || map->type != YAML_MAPPING_NODE) return NULL;
    for(yaml_node_pair_t* p = map->data.mapping.pairs.start; p < map->data.mapping.pairs.top; ++p){
        yaml_node_t* k = yaml_document_get_node(doc, p->key);
        yaml_node_t* v = yaml_document_get_node(doc, p->value);
        if(k && k->type == YAML_SCALAR_NODE && k->data.scalar.value &&
           strcmp((char*)k->data.scalar.value, key)==0)
            return v;
    }
    return NULL;
}

int parse_mqtt(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out){
    out->kind = KIND_MQTT;
    yaml_node_t* p = ymap_get(d, conn_map, "params");
    return parse_mqtt_params(d, p, &out->u.mqtt);
}
int parse_http_server(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out){
    out->kind = KIND_HTTP_SERVER;
    yaml_node_t* p = ymap_get(d, conn_map, "params");
    return parse_http_server_params(d, p, &out->u.http_server);
}
int parse_modbus_rtu(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out){
    out->kind = KIND_MODBUS_RTU;
    yaml_node_t* p = ymap_get(d, conn_map, "params");
    return parse_modbus_rtu_params(d, p, &out->u.modbus_rtu);
}
int parse_modbus_tcp(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out){
    out->kind = KIND_MODBUS_TCP;
    yaml_node_t* p = ymap_get(d, conn_map, "params");
    return parse_modbus_tcp_params(d, p, &out->u.modbus_tcp);
}
int parse_uart(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out){
    out->kind = KIND_UART;
    yaml_node_t* p = ymap_get(d, conn_map, "params");
    return parse_uart_params(d, p, &out->u.uart);
}
