// src/app.h
#pragma once
#include <stddef.h>

typedef struct {
    const char *cfg_file;   // e.g. "/etc/iotgw.yaml"
    const char *cfg_dir;    // e.g. "/etc/iotgwd" (optional; your loader may ignore)
    volatile int stop;      // set by signal handler
    volatile int reload;    // set on SIGHUP
} app_ctx_t;

// Run the gateway service loop (load config, start bridges, wait for signals)
int app_run(app_ctx_t *app);
