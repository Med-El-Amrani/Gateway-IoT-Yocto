#pragma once
#include <stddef.h>
#include "connector.h"

/* One mapping rule from a source key to a destination key */
typedef struct {
    const char *src_key;   /* e.g. "sensors/temp1" (meaning depends on connector) */
    const char *dst_key;   /* e.g. "factory/line1/temp" */
    double      scale;     /* optional: y = scale*x + offset (default 1.0) */
    double      offset;    /* optional: (default 0.0) */
} bridge_rule_t;

/* A bridge forwards data from src to dst using the rules above */
typedef struct {
    const char   *id;      /* e.g. "modbus_to_mqtt" */
    connector_t  *src;     /* must be non-NULL */
    connector_t  *dst;     /* must be non-NULL */
    bridge_rule_t *rules;  /* array of rules */
    size_t        nrules;  /* number of rules */
    int           enabled; /* 1 = active, 0 = ignored */
} bridge_t;

/* Apply all rules once: read from src, transform, write to dst.
 * Returns number of successful rule transfers, or <0 on fatal error.
 */
int  bridge_apply_once(bridge_t *b);

/* Convenience helpers */
size_t bridges_apply_all(bridge_t *arr, size_t n);

/* Typical main-loop helper: poll connectors, then apply all bridges */
void bridges_tick(connector_t *conns, size_t nconns,
                  bridge_t *bridges, size_t nbridges);
