#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "metrics.h"

int main(void) {
    metrics_state_t state;
    assert(metrics_state_init(&state) == 0);
    assert(metrics_state_healthy(&state) == 0);

    bridge_metrics_t bridge = {
        .bridge = "spi_to_mqtt",
        .connected = 0,
        .queue_depth = 3,
        .queue_dropped = 2,
        .published = 11,
        .publish_failures = 4,
    };
    assert(metrics_state_replace(&state, &bridge, 1) == 0);
    assert(metrics_state_healthy(&state) == 0);

    char *text = metrics_render_prometheus(&state);
    assert(text != NULL);
    assert(strstr(text, "iotgw_mqtt_connected{bridge=\"spi_to_mqtt\"} 0") != NULL);
    assert(strstr(text, "iotgw_queue_depth{bridge=\"spi_to_mqtt\"} 3") != NULL);
    assert(strstr(text, "iotgw_queue_dropped_total{bridge=\"spi_to_mqtt\"} 2") != NULL);
    assert(strstr(text, "iotgw_messages_published_total{bridge=\"spi_to_mqtt\"} 11") != NULL);
    assert(strstr(text, "iotgw_publish_failures_total{bridge=\"spi_to_mqtt\"} 4") != NULL);
    free(text);

    bridge.connected = 1;
    assert(metrics_state_replace(&state, &bridge, 1) == 0);
    assert(metrics_state_healthy(&state) == 1);
    char *health = metrics_render_health(&state);
    assert(health && strcmp(health, "{\"status\":\"ok\"}\n") == 0);
    free(health);

    metrics_state_destroy(&state);
    return 0;
}
