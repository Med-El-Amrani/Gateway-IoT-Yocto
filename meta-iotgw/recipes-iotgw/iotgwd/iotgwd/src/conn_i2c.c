#define _GNU_SOURCE
#include "conn_i2c.h"
#include "i2c_codec.h"
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int linux_open_bus(int bus, void *user) {
    (void)user;
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    return open(path, O_RDWR | O_CLOEXEC);
}

static int linux_read_register(int fd, uint8_t address, uint8_t reg,
                               uint8_t *data, size_t length, void *user) {
    (void)user;
    struct i2c_msg messages[2] = {
        {.addr = address, .flags = 0, .len = 1, .buf = &reg},
        {.addr = address, .flags = I2C_M_RD, .len = (uint16_t)length, .buf = data},
    };
    struct i2c_rdwr_ioctl_data transfer = {.msgs = messages, .nmsgs = 2};
    return ioctl(fd, I2C_RDWR, &transfer) == 2 ? 0 : -1;
}

static void linux_close_bus(int fd, void *user) { (void)user; close(fd); }

static void wait_for_next_poll(i2c_runtime_t *runtime) {
    for (int elapsed = 0; elapsed < 1000 && !atomic_load(&runtime->stop); elapsed += 100) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L};
        nanosleep(&delay, NULL);
    }
}

static void *poll_thread(void *arg) {
    i2c_runtime_t *runtime = arg;
    while (!atomic_load(&runtime->stop)) {
        int fd = runtime->io.open_bus(runtime->connector->params.bus, runtime->io.user);
        if (fd < 0) { wait_for_next_poll(runtime); continue; }
        int failed = 0;
        for (size_t d = 0; d < runtime->connector->params.devices_count && !failed; ++d) {
            const i2c_device_t *device = &runtime->connector->params.devices[d];
            const char *name = device->name ? device->name : "i2c-device";
            for (size_t p = 0; p < device->map_count; ++p) {
                const i2c_map_point_t *point = &device->map[p];
                uint8_t bytes[16];
                if (runtime->io.read_register(fd, device->addr, point->reg,
                                              bytes, point->len,
                                              runtime->io.user) != 0) {
                    failed = 1;
                    break;
                }
                char json[256];
                int len = i2c_format_json(json, sizeof(json), name, device->addr,
                                          point->reg, point->type,
                                          point->endianness_set ? point->endianness : I2C_BE,
                                          bytes, point->len, point->scale,
                                          point->has_scale);
                if (len > 0) runtime->on_rx((const uint8_t *)json, (size_t)len,
                                             runtime->on_rx_user);
            }
        }
        runtime->io.close_bus(fd, runtime->io.user);
        wait_for_next_poll(runtime);
    }
    return NULL;
}

static int valid_point(const i2c_map_point_t *point) {
    if (!point || point->len < 1 || point->len > 16) return 0;
    if (point->type == I2C_TYPE_BYTES) return 1;
    size_t expected = (point->type == I2C_TYPE_U8 || point->type == I2C_TYPE_S8) ? 1 :
                      (point->type == I2C_TYPE_U16 || point->type == I2C_TYPE_S16) ? 2 :
                      point->type == I2C_TYPE_U24 ? 3 : 4;
    return point->len == expected;
}

static int valid_connector(const i2c_connector_t *connector) {
    if (!connector || connector->params.bus < 0 || connector->params.bus > 32 ||
        !connector->params.devices || connector->params.devices_count == 0) return 0;
    for (size_t d = 0; d < connector->params.devices_count; ++d) {
        const i2c_device_t *device = &connector->params.devices[d];
        if (device->addr < 3 || device->addr > 119 || !device->map ||
            device->map_count == 0) return 0;
        for (size_t p = 0; p < device->map_count; ++p)
            if (!valid_point(&device->map[p])) return 0;
    }
    return 1;
}

int i2c_start_from_config_with_io(const i2c_connector_t *connector,
                                  i2c_runtime_t *runtime, i2c_msg_cb on_rx,
                                  void *user, const i2c_io_ops_t *io) {
    if (!connector || !runtime || !on_rx || !io || !io->open_bus ||
        !io->read_register || !io->close_bus || !valid_connector(connector))
        return -1;
    memset(runtime, 0, sizeof(*runtime));
    runtime->connector = connector;
    runtime->on_rx = on_rx;
    runtime->on_rx_user = user;
    runtime->io = *io;
    if (pthread_create(&runtime->thread, NULL, poll_thread, runtime) != 0) return -1;
    runtime->thread_started = 1;
    return 0;
}

int i2c_start_from_config(const i2c_connector_t *connector,
                          i2c_runtime_t *runtime, i2c_msg_cb on_rx, void *user) {
    const i2c_io_ops_t io = {.open_bus = linux_open_bus,
                             .read_register = linux_read_register,
                             .close_bus = linux_close_bus};
    return i2c_start_from_config_with_io(connector, runtime, on_rx, user, &io);
}

void i2c_stop(i2c_runtime_t *runtime) {
    if (!runtime) return;
    atomic_store(&runtime->stop, 1);
    if (runtime->thread_started) {
        pthread_join(runtime->thread, NULL);
        runtime->thread_started = 0;
    }
}
