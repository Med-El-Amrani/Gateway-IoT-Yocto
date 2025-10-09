// src/main_gateway.c (with logs)
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "config_types.h"
#include "bridge.h"
#include "config_loader.h"

// --- simple logging helpers ---
#define LOG(fmt, ...) do { fprintf(stderr, "[main] " fmt "\n", ##__VA_ARGS__); } while(0)

static const char* kind_str(int k){
    switch (k) {
        case KIND_SPI:          return "SPI";
        case KIND_MQTT:         return "MQTT";
        case KIND_HTTP_SERVER:  return "HTTP_SERVER";
        case KIND_UART:         return "UART";
        case KIND_I2C:          return "I2C";
        case KIND_MODBUS_RTU:   return "MODBUS_RTU";
        case KIND_MODBUS_TCP:   return "MODBUS_TCP";
        case KIND_COAP:         return "COAP";
        default:                return "UNKNOWN";
    }
}

static volatile int g_stop = 0;
static void on_sig(int s){ LOG("Signal %d received, stopping…", s); g_stop = 1; }

int main(int argc, char** argv){
    const char* cfg_path     = (argc > 1) ? argv[1] : "config.example.yaml";
    const char* topic_prefix = (argc > 2) ? argv[2] : "ingest";

    // ensure logs show up immediately
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    LOG("Starting iotgwd | cfg='%s' | topic_prefix='%s'", cfg_path, topic_prefix);

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    config_t cfg;
    if (config_load_file(cfg_path, &cfg) != 0) {
        LOG("ERROR: failed to load config: %s", cfg_path);
        return 1;
    }

    LOG("Config loaded: connectors=%zu, bridges=%zu",
        cfg.connectors.count, cfg.bridges.count);

    gw_bridge_runtime_t running[64];
    size_t running_count = 0;

    for (size_t i=0; i<cfg.bridges.count; ++i) {
        const bridge_t* br = &cfg.bridges.items[i];
        LOG("Bridge[%zu] name='%s' from='%s' to='%s'", i,
            br->name ? br->name : "(null)",
            br->from ? br->from : "(null)",
            br->to   ? br->to   : "(null)");

        if (prepare_bridge_runtime_t(&cfg, topic_prefix, br->name, br->from, br->to,
                                     &running[running_count]) != 0) {
            LOG("[%s] PREPARE failed (from:%s to:%s)",
                br->name, br->from, br->to);
            continue;
        }

        if (!running[running_count].from || !running[running_count].to) {
            LOG("[%s] missing connector (from:%s to:%s)",
                br->name, br->from, br->to);
            continue;
        }

        LOG("[%s] prepared: %s -> %s",
            br->name,
            kind_str(running[running_count].from->kind),
            kind_str(running[running_count].to->kind));

        if (gw_bridge_start(&running[running_count]) == 0) {
            LOG("[%s] STARTED (%s -> %s)",
                br->name,
                kind_str(running[running_count].from->kind),
                kind_str(running[running_count].to->kind));
            running_count++;
        } else {
            LOG("[bridge:%s] SKIP (pair %d→%d not supported yet)",
                br->name,
                (int)running[running_count].from->kind,
                (int)running[running_count].to->kind);
        }
    }

    if (running_count == 0) {
        LOG("No supported bridges started. Exiting.");
        config_free(&cfg);
        return 0;
    }

    LOG("Started %zu bridge(s). Gateway running. Ctrl+C to stop.", running_count);
    while (!g_stop) pause();

    LOG("Stopping bridges…");
    for (size_t i=0; i<running_count; ++i) gw_bridge_stop(&running[i]);
    config_free(&cfg);
    LOG("Bye.");
    return 0;
}
