#include "connector_registry.h"

/* Forward decls: put your real parser functions here (you already implemented some) */
int parse_mqtt(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_http_server(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_modbus_rtu(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_modbus_tcp(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_uart(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);

/* For not-yet-implemented types, parse = NULL -> opaque blob */
const connector_registry_entry_t CONNECTOR_REGISTRY[] = {
    {"mqtt",         CONN_KIND_MQTT,        parse_mqtt},
    {"modbus-rtu",   CONN_KIND_MODBUS_RTU,  parse_modbus_rtu},
    {"modbus-tcp",   CONN_KIND_MODBUS_TCP,  parse_modbus_tcp},
    {"http-server",  CONN_KIND_HTTP_SERVER, parse_http_server},
    {"uart",         CONN_KIND_UART,        parse_uart},

    {"spi",          CONN_KIND_SPI,         NULL},
    {"i2c",          CONN_KIND_I2C,         NULL},
    {"ble",          CONN_KIND_BLE,         NULL},
    {"coap",         CONN_KIND_COAP,        NULL},
    {"lorawan",      CONN_KIND_LORAWAN,     NULL},
    {"onewire",      CONN_KIND_ONEWIRE,     NULL},
    {"opcua",        CONN_KIND_OPCUA,       NULL},
    {"socketcan",    CONN_KIND_SOCKETCAN,   NULL},
    {"zigbee",       CONN_KIND_ZIGBEE,      NULL},
};

const size_t CONNECTOR_REGISTRY_LEN = sizeof(CONNECTOR_REGISTRY)/sizeof(CONNECTOR_REGISTRY[0]);

const connector_registry_entry_t* reg_lookup(const char* type){
    if(!type) return NULL;
    for(size_t i=0;i<CONNECTOR_REGISTRY_LEN;i++)
        if(strcmp(CONNECTOR_REGISTRY[i].type_str, type)==0) return &CONNECTOR_REGISTRY[i];
    return NULL;
}
