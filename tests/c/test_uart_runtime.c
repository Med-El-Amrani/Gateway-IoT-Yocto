#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <pty.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "conn_uart.h"

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    uint8_t payload[64];
    size_t length;
} received_t;

static void receive(const uint8_t *data, size_t len, void *user) {
    received_t *received = user;
    pthread_mutex_lock(&received->lock);
    assert(len <= sizeof(received->payload));
    memcpy(received->payload, data, len);
    received->length = len;
    pthread_cond_signal(&received->ready);
    pthread_mutex_unlock(&received->lock);
}

int main(void) {
    uint8_t parsed[2];
    assert(uart_parse_hex("0x0A7e", parsed, sizeof(parsed)) == 2);
    assert(parsed[0] == 0x0a && parsed[1] == 0x7e);

    int master = -1, slave = -1;
    char slave_name[128];
    assert(openpty(&master, &slave, slave_name, NULL, NULL) == 0);
    close(slave);

    uart_connector_t config = {0};
    config.params.port = slave_name;
    config.params.baudrate = 115200;
    config.params.bytesize = 8;
    config.params.bytesize_set = true;
    config.params.parity = 'N';
    config.params.parity_set = true;
    config.params.stopbits = 1.0;
    config.params.stopbits_set = true;
    config.params.timeout_ms = 100;
    config.params.timeout_set = true;
    config.params.has_packet = true;
    config.params.packet.end = "0A";

    received_t received = {
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .ready = PTHREAD_COND_INITIALIZER,
    };
    uart_runtime_t runtime;
    assert(uart_start_from_config(&config, &runtime, receive, &received) == UART_OK);
    assert(write(master, "temperature=21\n", 15) == 15);

    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&received.lock);
    while (received.length == 0)
        assert(pthread_cond_timedwait(&received.ready, &received.lock, &deadline) == 0);
    assert(received.length == 14);
    assert(memcmp(received.payload, "temperature=21", 14) == 0);
    pthread_mutex_unlock(&received.lock);

    uart_stop(&runtime);
    close(master);
    pthread_cond_destroy(&received.ready);
    pthread_mutex_destroy(&received.lock);
    return 0;
}
