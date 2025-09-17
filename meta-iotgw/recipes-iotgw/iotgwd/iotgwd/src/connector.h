#pragma once
#include <stddef.h>

/* Supported connector types (extend as you add protocols) */
typedef enum {
    CONN_NONE = 0,
    CONN_MQTT = 1,
    CONN_SPI  = 2,
    /* later: CONN_MODBUS_RTU, CONN_MODBUS_TCP, CONN_ZIGBEE, ... */
} connector_type_t;

/* Opaque forward-declaration for the connector object */
typedef struct connector connector_t;

/* I/O operations (return 0 on success, <0 on error like -ENOSYS/-EIO) */
typedef int  (*conn_read_fn)(connector_t *c, const char *key, double *out);
typedef int  (*conn_write_fn)(connector_t *c, const char *key, double val);
typedef void (*conn_poll_fn)(connector_t *c);

/* Generic connector “vtable” + instance data */
struct connector {
    const char      *id;    /* e.g., "mqtt_local" */
    connector_type_t type;
    void            *impl;  /* protocol-specific context (owned by creator) */

    conn_read_fn  read;     /* optional; can be NULL */
    conn_write_fn write;    /* optional; can be NULL */
    conn_poll_fn  poll;     /* optional; can be NULL */
};

/* Helpers */
void connectors_poll(connector_t *arr, size_t n);

/* Optional convenience wrappers with safe defaults */
int connector_read (connector_t *c, const char *key, double *out);
int connector_write(connector_t *c, const char *key, double val);
