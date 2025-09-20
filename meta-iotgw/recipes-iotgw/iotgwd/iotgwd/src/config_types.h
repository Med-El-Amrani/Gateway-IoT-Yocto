#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "connectors.h"  // All protocol-specific structs (you already have these)
#include "gw_msg.h"   // <-- pour avoir kind_t


/* Opaque params (for types whose parser isn’t implemented yet).
 * We keep a normalized JSON string of the "params" sub-tree so code that
 * runs later (drivers) can parse it easily. */
typedef struct {
    char *json_params;   // serialized JSON of params
} connector_opaque_t;

/* One connector instance = name + kind + union of all possibilities */
typedef struct {
    char *name;               // key in "connectors"
    kind_t kind;
    char **tags;              // optional, from schema
    size_t tags_count;
    union {
        mqtt_connector_t          mqtt;
        modbus_rtu_connector_t    modbus_rtu;
        modbus_tcp_connector_t    modbus_tcp;
        http_server_connector_t   http_server;
        uart_connector_t          uart;
        spi_connector_t           spi;
        i2c_connector_t           i2c;
        ble_connector_t           ble;
        coap_connector_t          coap;
        lorawan_connector_t       lorawan;
        onewire_connector_t       onewire;
        opcua_connector_t         opcua;
        socketcan_connector_t     socketcan;
        zigbee_connector_t        zigbee;
        connector_opaque_t        opaque;  // fallback
    } u;
} connector_any_t;

typedef struct {
    connector_any_t *items;
    size_t count;
} connectors_table_t;

/* Gateway (per schema) */
typedef struct {
    char *name;
    char *timezone;
    char *loglevel;    // "trace"|"debug"|"info"|"warn"|"error"
    char *logfile;
    int   metrics_port;
    bool  metrics_port_set;
} gateway_cfg_t;

typedef struct {
    char **paths;
    size_t count;
} include_list_t;

/* Bridges (per schema) */
typedef enum { MAP_FMT_JSON, MAP_FMT_KV, MAP_FMT_RAW } map_format_t;

typedef struct {
    char   *topic;
    map_format_t format;
    char **fields;
    size_t fields_count;
    bool   timestamp;
    bool   timestamp_set;
} bridge_mapping_t;

typedef struct {
    double max_msgs_per_sec; bool has_max_msgs_per_sec;
    int    burst;            bool has_burst;
} bridge_rate_limit_t;

typedef enum { BUF_DROP_OLDEST, BUF_DROP_NEW } buffer_policy_t;

typedef struct {
    int size;                bool has_size;
    buffer_policy_t policy;  bool has_policy;
} bridge_buffer_t;

typedef struct {
    char *name;
    char *from;
    char *to;
    bridge_mapping_t mapping;
    char **transform; size_t transform_count;
    bridge_rate_limit_t rate_limit;
    bridge_buffer_t buffer;
} bridge_t;

typedef struct {
    bridge_t *items;
    size_t count;
} bridges_table_t;

/* Whole config */
typedef struct {
    double version;     bool version_set;
    gateway_cfg_t gateway;
    include_list_t includes;
    connectors_table_t connectors;
    bridges_table_t bridges;
} config_t;

#endif /* CONFIG_TYPES_H */
