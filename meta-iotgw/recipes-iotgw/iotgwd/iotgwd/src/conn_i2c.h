#pragma once
#include <pthread.h>
#include <stdatomic.h>
#include "config_types.h"

typedef void (*i2c_msg_cb)(const uint8_t *data, size_t len, void *user);
typedef struct {
    int (*open_bus)(int bus, void *user);
    int (*read_register)(int fd, uint8_t address, uint8_t reg,
                         uint8_t *data, size_t length, void *user);
    void (*close_bus)(int fd, void *user);
    void *user;
} i2c_io_ops_t;

typedef struct {
    pthread_t thread;
    atomic_int stop;
    int thread_started;
    const i2c_connector_t *connector;
    i2c_msg_cb on_rx;
    void *on_rx_user;
    i2c_io_ops_t io;
} i2c_runtime_t;

int i2c_start_from_config(const i2c_connector_t *connector,
                          i2c_runtime_t *runtime, i2c_msg_cb on_rx, void *user);
int i2c_start_from_config_with_io(const i2c_connector_t *connector,
                                  i2c_runtime_t *runtime, i2c_msg_cb on_rx,
                                  void *user, const i2c_io_ops_t *io);
void i2c_stop(i2c_runtime_t *runtime);
