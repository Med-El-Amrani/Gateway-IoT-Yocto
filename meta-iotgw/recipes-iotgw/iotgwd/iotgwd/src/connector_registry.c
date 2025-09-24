#include "connector_registry.h"

/* Forward decls: put your real parser functions here (you already implemented some) */
int parse_mqtt(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_http_server(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_modbus_rtu(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_modbus_tcp(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_uart(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);
int parse_spi(yaml_document_t* d, yaml_node_t* conn_map, connector_any_t* out);

/* For not-yet-implemented types, parse = NULL -> opaque blob */
const connector_registry_entry_t CONNECTOR_REGISTRY[] = {
    {"mqtt",         KIND_MQTT,        parse_mqtt},
    {"modbus-rtu",   KIND_MODBUS_RTU,  parse_modbus_rtu},
    {"modbus-tcp",   KIND_MODBUS_TCP,  parse_modbus_tcp},
    {"http-server",  KIND_HTTP_SERVER, parse_http_server},
    {"uart",         KIND_UART,        parse_uart},

    {"spi",          KIND_SPI,         parse_spi},
    {"i2c",          KIND_I2C,         NULL},
    {"ble",          KIND_BLE,         NULL},
    {"coap",         KIND_COAP,        NULL},
    {"lorawan",      KIND_LORAWAN,     NULL},
    {"onewire",      KIND_ONEWIRE,     NULL},
    {"opcua",        KIND_OPCUA,       NULL},
    {"socketcan",    KIND_SOCKETCAN,   NULL},
    {"zigbee",       KIND_ZIGBEE,      NULL},
};

const size_t CONNECTOR_REGISTRY_LEN = sizeof(CONNECTOR_REGISTRY)/sizeof(CONNECTOR_REGISTRY[0]);

const connector_registry_entry_t* reg_lookup(const char* type){
    if(!type) return NULL;
    for(size_t i=0;i<CONNECTOR_REGISTRY_LEN;i++)
        if(strcmp(CONNECTOR_REGISTRY[i].type_str, type)==0) return &CONNECTOR_REGISTRY[i];
    return NULL;
}
