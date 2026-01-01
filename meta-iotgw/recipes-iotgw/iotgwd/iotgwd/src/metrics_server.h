#pragma once

#include <microhttpd.h>
#include "metrics.h"

typedef struct {
    struct MHD_Daemon *daemon;
    metrics_state_t *state;
    int port;
} metrics_server_t;

int metrics_server_start(metrics_server_t *server, int port, metrics_state_t *state);
void metrics_server_stop(metrics_server_t *server);
