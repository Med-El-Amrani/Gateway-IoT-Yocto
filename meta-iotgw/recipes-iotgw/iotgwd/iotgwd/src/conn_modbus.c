#include "conn_modbus.h"
#include "modbus_codec.h"

#include <errno.h>
#include <modbus/modbus.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void sleep_interruptible(modbus_runtime_t *runtime, unsigned int ms) {
    while (ms && !atomic_load(&runtime->stop)) {
        unsigned int slice = ms > 100 ? 100 : ms;
        struct timespec delay = {.tv_sec = 0, .tv_nsec = (long)slice * 1000000L};
        nanosleep(&delay, NULL);
        ms -= slice;
    }
}

static int read_point(modbus_t *context, modbus_func_t function,
                      int address, int count, uint16_t *registers) {
    if (function == MODBUS_FUNC_HOLDING)
        return modbus_read_registers(context, address, count, registers);
    if (function == MODBUS_FUNC_INPUT)
        return modbus_read_input_registers(context, address, count, registers);
    uint8_t bits[4] = {0};
    int rc = function == MODBUS_FUNC_COIL
           ? modbus_read_bits(context, address, count, bits)
           : modbus_read_input_bits(context, address, count, bits);
    if (rc > 0) for (int i = 0; i < rc; ++i) registers[i] = bits[i];
    return rc;
}

static int read_point_with_retries(modbus_t *context, modbus_func_t function,
                                   int address, int count, uint16_t *registers,
                                   int retries) {
    int rc;
    do {
        rc = read_point(context, function, address, count, registers);
    } while (rc != count && retries-- > 0);
    return rc;
}

static void emit_point(modbus_runtime_t *runtime, const char *name, int unit_id,
                       modbus_datatype_t type, double scale, int has_scale,
                       const uint16_t *registers, size_t count) {
    double value;
    char json[256];
    if (modbus_decode_value(type, registers, count, scale, has_scale, &value) != 0)
        return;
    int length = modbus_format_json(json, sizeof(json), name, unit_id, value);
    if (length > 0) runtime->on_rx((const uint8_t *)json, (size_t)length, runtime->user);
}

static modbus_datatype_t effective_type(modbus_datatype_t type,
                                        int has_signed, int signed_flag) {
    if (!has_signed || !signed_flag) return type;
    if (type == MODBUS_TYPE_U16) return MODBUS_TYPE_S16;
    if (type == MODBUS_TYPE_U32) return MODBUS_TYPE_S32;
    return type;
}

static modbus_t *new_context(modbus_runtime_t *runtime) {
    if (runtime->kind == KIND_MODBUS_RTU) {
        const modbus_rtu_params_t *p = &runtime->connector->u.modbus_rtu.params;
        return modbus_new_rtu(p->port, p->baudrate, p->parity, 8, p->stopbits);
    }
    const modbus_tcp_params_t *p = &runtime->connector->u.modbus_tcp.params;
    return modbus_new_tcp(p->host, p->port_set ? p->port : 502);
}

static int poll_rtu(modbus_runtime_t *runtime, modbus_t *context) {
    const modbus_rtu_params_t *p = &runtime->connector->u.modbus_rtu.params;
    for (size_t s = 0; s < p->slaves_count && !atomic_load(&runtime->stop); ++s) {
        const modbus_slave_t *slave = &p->slaves[s];
        if (modbus_set_slave(context, slave->unit_id) != 0) return -1;
        for (size_t i = 0; i < slave->map_count; ++i) {
            const modbus_point_t *point = &slave->map[i];
            uint16_t registers[4] = {0};
            int rc = read_point(context, point->func, point->addr,
                                point->count, registers);
            if (rc != point->count) return -1;
            emit_point(runtime, point->name, slave->unit_id,
                       effective_type(point->type, point->has_signed,
                                      point->signed_flag),
                       point->scale, point->has_scale, registers, point->count);
        }
        sleep_interruptible(runtime, slave->poll_ms);
    }
    return 0;
}

static int poll_tcp(modbus_runtime_t *runtime, modbus_t *context) {
    const modbus_tcp_params_t *p = &runtime->connector->u.modbus_tcp.params;
    int unit = p->unit_id_set ? p->unit_id : 1;
    if (modbus_set_slave(context, unit) != 0) return -1;
    for (size_t i = 0; i < p->map_count; ++i) {
        const modbus_tcp_point_t *point = &p->map[i];
        uint16_t registers[4] = {0};
        int retries = p->retries_set ? p->retries : 2;
        int rc = read_point_with_retries(context, point->func, point->addr,
                                         point->count, registers, retries);
        if (rc != point->count) return -1;
        emit_point(runtime, point->name, unit,
                   effective_type(point->type, point->has_signed,
                                  point->signed_flag),
                   point->scale, point->has_scale, registers, point->count);
    }
    sleep_interruptible(runtime, 1000);
    return 0;
}

static void *poll_thread(void *arg) {
    modbus_runtime_t *runtime = arg;
    while (!atomic_load(&runtime->stop)) {
        modbus_t *context = new_context(runtime);
        if (!context) { sleep_interruptible(runtime, 1000); continue; }
        int timeout_ms = runtime->kind == KIND_MODBUS_RTU
                       ? runtime->connector->u.modbus_rtu.params.timeout_ms
                       : runtime->connector->u.modbus_tcp.params.timeout_ms;
        modbus_set_response_timeout(context, timeout_ms / 1000,
                                    (timeout_ms % 1000) * 1000);
        if (modbus_connect(context) == 0) {
            while (!atomic_load(&runtime->stop)) {
                int rc = runtime->kind == KIND_MODBUS_RTU
                       ? poll_rtu(runtime, context) : poll_tcp(runtime, context);
                if (rc != 0) break;
            }
            modbus_close(context);
        }
        modbus_free(context);
        sleep_interruptible(runtime, 1000);
    }
    return NULL;
}

static int valid_point(const char *name, modbus_datatype_t type, size_t count) {
    size_t required = type == MODBUS_TYPE_DOUBLE ? 4 :
                      (type == MODBUS_TYPE_U32 || type == MODBUS_TYPE_S32 ||
                       type == MODBUS_TYPE_FLOAT) ? 2 : 1;
    return name && name[0] && count >= required && count <= 4;
}

static int validate_connector(const connector_any_t *connector) {
    if (connector->kind == KIND_MODBUS_RTU) {
        const modbus_rtu_params_t *p = &connector->u.modbus_rtu.params;
        if (!p->port || p->timeout_ms <= 0 || p->slaves_count == 0) return -1;
        for (size_t s = 0; s < p->slaves_count; ++s) {
            if (p->slaves[s].map_count == 0 || p->slaves[s].poll_ms == 0) return -1;
            for (size_t i = 0; i < p->slaves[s].map_count; ++i)
                if (!valid_point(p->slaves[s].map[i].name,
                                 p->slaves[s].map[i].type,
                                 p->slaves[s].map[i].count)) return -1;
        }
        return 0;
    }
    const modbus_tcp_params_t *p = &connector->u.modbus_tcp.params;
    if (!p->host || p->timeout_ms <= 0 || p->map_count == 0) return -1;
    for (size_t i = 0; i < p->map_count; ++i)
        if (!valid_point(p->map[i].name, p->map[i].type, p->map[i].count)) return -1;
    return 0;
}

int modbus_start_from_config(const connector_any_t *connector,
                             modbus_runtime_t *runtime,
                             modbus_msg_cb on_rx, void *user) {
    if (!connector || !runtime || !on_rx ||
        (connector->kind != KIND_MODBUS_RTU && connector->kind != KIND_MODBUS_TCP))
        return -1;
    if (validate_connector(connector) != 0) return -1;
    memset(runtime, 0, sizeof(*runtime));
    runtime->kind = connector->kind;
    runtime->connector = connector;
    runtime->on_rx = on_rx;
    runtime->user = user;
    if (pthread_create(&runtime->thread, NULL, poll_thread, runtime) != 0) return -1;
    runtime->thread_started = 1;
    return 0;
}

void modbus_stop(modbus_runtime_t *runtime) {
    if (!runtime) return;
    atomic_store(&runtime->stop, 1);
    if (runtime->thread_started) {
        pthread_join(runtime->thread, NULL);
        runtime->thread_started = 0;
    }
}
