#pragma once
#include <stdint.h>

typedef struct {
    const char *cfg_file;   // default: /etc/iotgw.yaml
    const char *cfg_dir;    // default: /etc/iotgwd
    int         stop;       // set by signal
    int         reload;     // set by signal
} app_ctx_t;

// returns 0 on clean exit; non-zero on error
int app_run(app_ctx_t *app);
