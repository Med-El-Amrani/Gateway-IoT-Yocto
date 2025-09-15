#pragma once
#include <stddef.h>

typedef struct {
    // TODO: Fill with parsed, validated configuration (connectors, bridges, etc.)
    // For now, just counters so code runs and can be tested.
    size_t files_loaded;
    size_t fragments_loaded;
} runtime_cfg_t;

void runtime_reset(runtime_cfg_t *rt);
