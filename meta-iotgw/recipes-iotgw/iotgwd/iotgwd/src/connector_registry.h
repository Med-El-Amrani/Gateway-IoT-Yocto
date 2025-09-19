#ifndef CONNECTOR_REGISTRY_H
#define CONNECTOR_REGISTRY_H

#include "config_types.h"
#include <yaml.h>

/* A parser takes the YAML mapping node of connector (with keys: type, params, tags)
 * and fills the right union member in `out`. Returns 0 on success. */
typedef int (*connector_parser_fn)(yaml_document_t* doc, yaml_node_t* connector_map, connector_any_t* out);

/* Registry entry */
typedef struct {
    const char *type_str;         // e.g., "mqtt"
    connector_kind_t kind;
    connector_parser_fn parse;    // can be NULL => use opaque fallback
} connector_registry_entry_t;

/* Global registry defined in connector_registry.c */
extern const connector_registry_entry_t CONNECTOR_REGISTRY[];
extern const size_t CONNECTOR_REGISTRY_LEN;

/* Find registry entry by type string */
const connector_registry_entry_t* reg_lookup(const char* type);

#endif
