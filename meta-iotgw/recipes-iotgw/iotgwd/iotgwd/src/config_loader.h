#pragma once
#include "config_types.h"

// Load main config file then merge fragments from confdir.
// Returns 0 on success; non-zero if any load/parse/validate failed.
int  config_load_file(const char* path, config_t* cfg);
void config_free(config_t* cfg);
const connector_any_t* config_find_connector(const config_t* cfg, const char* name);
