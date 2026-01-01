#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int metrics_state_init(metrics_state_t *state) {
    if (!state) return -1;
    memset(state, 0, sizeof(*state));
    if (pthread_mutex_init(&state->lock, NULL) != 0) return -1;
    state->initialized = 1;
    return 0;
}

void metrics_state_destroy(metrics_state_t *state) {
    if (!state || !state->initialized) return;
    pthread_mutex_lock(&state->lock);
    free(state->bridges);
    state->bridges = NULL;
    state->count = 0;
    pthread_mutex_unlock(&state->lock);
    pthread_mutex_destroy(&state->lock);
    state->initialized = 0;
}

int metrics_state_replace(metrics_state_t *state,
                          const bridge_metrics_t *bridges, size_t count) {
    if (!state || !state->initialized || (count && !bridges)) return -1;
    bridge_metrics_t *copy = count ? malloc(count * sizeof(*copy)) : NULL;
    if (count && !copy) return -1;
    if (count) memcpy(copy, bridges, count * sizeof(*copy));

    pthread_mutex_lock(&state->lock);
    free(state->bridges);
    state->bridges = copy;
    state->count = count;
    pthread_mutex_unlock(&state->lock);
    return 0;
}

int metrics_state_healthy(metrics_state_t *state) {
    if (!state || !state->initialized) return 0;
    pthread_mutex_lock(&state->lock);
    int healthy = state->count > 0;
    for (size_t i = 0; healthy && i < state->count; ++i)
        healthy = state->bridges[i].connected;
    pthread_mutex_unlock(&state->lock);
    return healthy;
}

char *metrics_render_prometheus(metrics_state_t *state) {
    if (!state || !state->initialized) return NULL;
    pthread_mutex_lock(&state->lock);
    size_t capacity = 512 + state->count * 1024;
    char *output = malloc(capacity);
    if (!output) { pthread_mutex_unlock(&state->lock); return NULL; }
    size_t used = 0;
#define APPEND(...) do { \
    int written = snprintf(output + used, capacity - used, __VA_ARGS__); \
    if (written < 0 || (size_t)written >= capacity - used) { \
        free(output); pthread_mutex_unlock(&state->lock); return NULL; \
    } \
    used += (size_t)written; \
} while (0)
    APPEND("# HELP iotgw_mqtt_connected Whether the MQTT destination is connected.\n");
    APPEND("# TYPE iotgw_mqtt_connected gauge\n");
    for (size_t i = 0; i < state->count; ++i) {
        const bridge_metrics_t *m = &state->bridges[i];
        APPEND("iotgw_mqtt_connected{bridge=\"%s\"} %d\n", m->bridge, m->connected);
        APPEND("iotgw_queue_depth{bridge=\"%s\"} %llu\n", m->bridge,
               (unsigned long long)m->queue_depth);
        APPEND("iotgw_queue_dropped_total{bridge=\"%s\"} %llu\n", m->bridge,
               (unsigned long long)m->queue_dropped);
        APPEND("iotgw_messages_published_total{bridge=\"%s\"} %llu\n", m->bridge,
               (unsigned long long)m->published);
        APPEND("iotgw_publish_failures_total{bridge=\"%s\"} %llu\n", m->bridge,
               (unsigned long long)m->publish_failures);
    }
#undef APPEND
    pthread_mutex_unlock(&state->lock);
    return output;
}

char *metrics_render_health(metrics_state_t *state) {
    int healthy = metrics_state_healthy(state);
    const char *body = healthy ? "{\"status\":\"ok\"}\n"
                               : "{\"status\":\"unavailable\"}\n";
    size_t size = strlen(body) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, body, size);
    return copy;
}
