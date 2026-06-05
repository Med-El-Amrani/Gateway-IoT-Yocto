#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "conn_modbus.h"
#include <modbus/modbus.h>

struct _modbus { int unit; };
modbus_t *modbus_new_rtu(const char *a, int b, char c, int d, int e) {
    (void)a; (void)b; (void)c; (void)d; (void)e; return calloc(1, sizeof(modbus_t));
}
modbus_t *modbus_new_tcp(const char *host, int port) {
    (void)host; (void)port; return calloc(1, sizeof(modbus_t));
}
int modbus_set_slave(modbus_t *ctx, int unit) { ctx->unit = unit; return 0; }
int modbus_set_response_timeout(modbus_t *ctx, uint32_t s, uint32_t us) {
    (void)ctx; (void)s; (void)us; return 0;
}
int modbus_connect(modbus_t *ctx) { (void)ctx; return 0; }
void modbus_close(modbus_t *ctx) { (void)ctx; }
void modbus_free(modbus_t *ctx) { free(ctx); }
int modbus_read_registers(modbus_t *ctx, int addr, int count, uint16_t *dest) {
    (void)ctx; (void)addr; assert(count == 2);
    dest[0] = 0x4148; dest[1] = 0x0000; return 2;
}
int modbus_read_input_registers(modbus_t *c, int a, int n, uint16_t *d) {
    return modbus_read_registers(c, a, n, d);
}
int modbus_read_bits(modbus_t *c, int a, int n, uint8_t *d) {
    (void)c; (void)a; memset(d, 1, (size_t)n); return n;
}
int modbus_read_input_bits(modbus_t *c, int a, int n, uint8_t *d) {
    return modbus_read_bits(c, a, n, d);
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    char payload[256];
} received_t;

static void receive(const uint8_t *data, size_t len, void *user) {
    received_t *received = user;
    pthread_mutex_lock(&received->lock);
    assert(len < sizeof(received->payload));
    memcpy(received->payload, data, len);
    received->payload[len] = '\0';
    pthread_cond_signal(&received->ready);
    pthread_mutex_unlock(&received->lock);
}

int main(void) {
    modbus_tcp_point_t point = {
        .name = "power_kw", .func = MODBUS_FUNC_HOLDING,
        .addr = 100, .count = 2, .type = MODBUS_TYPE_FLOAT,
    };
    connector_any_t connector = {.kind = KIND_MODBUS_TCP};
    connector.u.modbus_tcp.params.host = "127.0.0.1";
    connector.u.modbus_tcp.params.port = 502;
    connector.u.modbus_tcp.params.port_set = true;
    connector.u.modbus_tcp.params.unit_id = 3;
    connector.u.modbus_tcp.params.unit_id_set = true;
    connector.u.modbus_tcp.params.timeout_ms = 100;
    connector.u.modbus_tcp.params.map = &point;
    connector.u.modbus_tcp.params.map_count = 1;

    received_t received = {.lock = PTHREAD_MUTEX_INITIALIZER,
                           .ready = PTHREAD_COND_INITIALIZER};
    modbus_runtime_t runtime;
    assert(modbus_start_from_config(&connector, &runtime, receive, &received) == 0);
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&received.lock);
    while (!received.payload[0])
        assert(pthread_cond_timedwait(&received.ready, &received.lock, &deadline) == 0);
    assert(strcmp(received.payload,
                  "{\"name\":\"power_kw\",\"unit_id\":3,\"value\":12.5}") == 0);
    pthread_mutex_unlock(&received.lock);
    modbus_stop(&runtime);
    pthread_cond_destroy(&received.ready);
    pthread_mutex_destroy(&received.lock);
    return 0;
}
