#pragma once

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "config_types.h"

typedef void (*modbus_msg_cb)(const uint8_t *data, size_t len, void *user);

typedef struct {
    pthread_t thread;
    atomic_int stop;
    int thread_started;
    kind_t kind;
    const connector_any_t *connector;
    modbus_msg_cb on_rx;
    void *user;
} modbus_runtime_t;

int modbus_start_from_config(const connector_any_t *connector,
                             modbus_runtime_t *runtime,
                             modbus_msg_cb on_rx, void *user);
void modbus_stop(modbus_runtime_t *runtime);
