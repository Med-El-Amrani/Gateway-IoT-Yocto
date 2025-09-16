#pragma once
#include "runtime.h"

// Load main config file then merge fragments from confdir.
// Returns 0 on success; non-zero if any load/parse/validate failed.
int config_load_all(runtime_cfg_t *rt, const char *main_path, const char *confdir);
