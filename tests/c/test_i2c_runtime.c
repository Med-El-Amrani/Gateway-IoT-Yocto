#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "conn_i2c.h"

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    char payload[256];
    int opens;
    int closes;
} fixture_t;

static int open_bus(int bus, void *user) {
    fixture_t *fixture = user; assert(bus == 1); fixture->opens++; return 42;
}
static int read_register(int fd, uint8_t addr, uint8_t reg,
                         uint8_t *data, size_t len, void *user) {
    (void)user; assert(fd == 42 && addr == 0x48 && reg == 0 && len == 2);
    data[0] = 0x01; data[1] = 0x50; return 0;
}
static void close_bus(int fd, void *user) {
    fixture_t *fixture = user; assert(fd == 42); fixture->closes++;
}
static void receive(const uint8_t *data, size_t len, void *user) {
    fixture_t *fixture = user;
    pthread_mutex_lock(&fixture->lock);
    assert(len < sizeof(fixture->payload));
    memcpy(fixture->payload, data, len); fixture->payload[len] = '\0';
    pthread_cond_signal(&fixture->ready);
    pthread_mutex_unlock(&fixture->lock);
}

int main(void) {
    i2c_map_point_t point = {.reg = 0, .len = 2, .type = I2C_TYPE_S16,
                             .endianness = I2C_BE, .endianness_set = true,
                             .scale = 0.0625, .has_scale = true};
    i2c_device_t device = {.addr = 0x48, .name = "temp_sensor",
                           .map_count = 1, .map = &point};
    i2c_connector_t connector = {.params = {.bus = 1, .devices_count = 1,
                                             .devices = &device}};
    fixture_t fixture = {.lock = PTHREAD_MUTEX_INITIALIZER,
                         .ready = PTHREAD_COND_INITIALIZER};
    i2c_io_ops_t io = {.open_bus = open_bus, .read_register = read_register,
                       .close_bus = close_bus, .user = &fixture};
    i2c_runtime_t runtime;
    assert(i2c_start_from_config_with_io(&connector, &runtime, receive,
                                         &fixture, &io) == 0);
    struct timespec deadline; clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_sec += 2;
    pthread_mutex_lock(&fixture.lock);
    while (!fixture.payload[0])
        assert(pthread_cond_timedwait(&fixture.ready, &fixture.lock, &deadline) == 0);
    assert(strstr(fixture.payload, "\"value\":21") != NULL);
    pthread_mutex_unlock(&fixture.lock);
    i2c_stop(&runtime);
    assert(fixture.opens >= 1 && fixture.closes >= 1);
    pthread_cond_destroy(&fixture.ready); pthread_mutex_destroy(&fixture.lock);
    return 0;
}
