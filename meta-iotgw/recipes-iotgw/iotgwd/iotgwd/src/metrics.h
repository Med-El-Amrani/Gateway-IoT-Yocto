#pragma once

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char bridge[128];
    int connected;
    uint64_t queue_depth;
    uint64_t queue_dropped;
    uint64_t published;
    uint64_t publish_failures;
} bridge_metrics_t;

typedef struct {
    bridge_metrics_t *bridges;
    size_t count;
    pthread_mutex_t lock;
    int initialized;
} metrics_state_t;

int metrics_state_init(metrics_state_t *state);
void metrics_state_destroy(metrics_state_t *state);
int metrics_state_replace(metrics_state_t *state,
                          const bridge_metrics_t *bridges, size_t count);
int metrics_state_healthy(metrics_state_t *state);
char *metrics_render_prometheus(metrics_state_t *state);
char *metrics_render_health(metrics_state_t *state);
