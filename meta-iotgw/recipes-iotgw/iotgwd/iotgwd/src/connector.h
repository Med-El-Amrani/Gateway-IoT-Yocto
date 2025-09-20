#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "config_types.h"

// message générique qui circule dans les bridges
typedef struct {
    const char *topic;
    uint8_t *data;
    size_t len;
    double timestamp;   // epoch s, si besoin
} gw_msg_t;

typedef struct connector connector_t;
typedef void (*gw_rx_cb)(const gw_msg_t *msg, void *user);

typedef struct {
    int  (*open)(connector_t *c, const connector_any_t *cfg);
    int  (*start)(connector_t *c);
    void (*stop)(connector_t *c);
    void (*close)(connector_t *c);

    int  (*set_rx_cb)(connector_t *c, gw_rx_cb cb, void *user);
    int  (*send)(connector_t *c, const gw_msg_t *msg);

    uint64_t caps;
} connector_ops_t;

struct connector {
    const char *id;
    kind_t kind;
    const connector_ops_t *ops;
    void *state; // opaque (driver interne)
};

