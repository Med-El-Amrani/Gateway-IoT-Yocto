// src/app.c
#include "app.h"
#include "bridge.h"
#include "config_loader.h"
#include "config_types.h"
#include "sdwrap.h"
#include "metrics.h"
#include "metrics_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

static app_ctx_t *g_app = NULL;

static void on_sigint(int sig){ (void)sig; if (g_app) g_app->stop = 1; }
static void on_sigterm(int sig){ (void)sig; if (g_app) g_app->stop = 1; }
static void on_sighup(int sig){ (void)sig; if (g_app) g_app->reload = 1; }

static int start_all_bridges(const config_t *cfg, const char *topic_prefix,
                             gw_bridge_runtime_t **out_arr, size_t *out_cnt)
{
    size_t cap = cfg->bridges.count;
    gw_bridge_runtime_t *arr = (gw_bridge_runtime_t*)calloc(cap ? cap : 1, sizeof(*arr));
    if (!arr) return -1;

    size_t n = 0;
    size_t failures = 0;
    for (size_t i = 0; i < cfg->bridges.count; ++i) {
        const bridge_t *br = &cfg->bridges.items[i];

        if (prepare_bridge_runtime_t(cfg, topic_prefix, br->name, br->from, br->to, &arr[n]) != 0) {
            fprintf(stderr, "[%s] prepare failed (from:%s to:%s)\n",
                    br->name ? br->name : "bridge",
                    br->from ? br->from : "(missing)",
                    br->to ? br->to : "(missing)");
            failures++;
            continue;
        }
        if (!arr[n].from || !arr[n].to) {
            fprintf(stderr, "[%s] missing connector (from:%s to:%s)\n", br->name, br->from, br->to);
            continue;
        }
        if (gw_bridge_start(&arr[n]) == 0) {
            n++;
        } else {
            fprintf(stderr, "[bridge:%s] skip (pair %d→%d not supported yet)\n",
                    br->name, (int)arr[n].from->kind, (int)arr[n].to->kind);
            /* start may have opened the destination before the source failed. */
            gw_bridge_stop(&arr[n]);
            memset(&arr[n], 0, sizeof(arr[n]));
            failures++;
        }
    }

    *out_arr = arr;
    *out_cnt = n;
    return failures == 0 ? 0 : -1;
}

static void stop_all_bridges(gw_bridge_runtime_t *arr, size_t n){
    if (!arr) return;
    for (size_t i = 0; i < n; ++i) gw_bridge_stop(&arr[i]);
    free(arr);
}

static int refresh_metrics(metrics_state_t *state,
                           gw_bridge_runtime_t *bridges, size_t count) {
    bridge_metrics_t *snapshot = count ? calloc(count, sizeof(*snapshot)) : NULL;
    if (count && !snapshot) return -1;
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!bridges[i].to || bridges[i].to->kind != KIND_MQTT ||
            !bridges[i].dest_ctx) continue;
        mqtt_runtime_t *mqtt = bridges[i].dest_ctx;
        snprintf(snapshot[used].bridge, sizeof(snapshot[used].bridge), "%s",
                 bridges[i].id[0] ? bridges[i].id : "bridge");
        snapshot[used].connected = mqtt_is_connected(mqtt);
        snapshot[used].queue_depth = mqtt_queued_messages(mqtt);
        snapshot[used].queue_dropped = mqtt_dropped_messages(mqtt);
        snapshot[used].published = mqtt_published_messages(mqtt);
        snapshot[used].publish_failures = mqtt_publish_failures(mqtt);
        used++;
    }
    int rc = metrics_state_replace(state, snapshot, used);
    free(snapshot);
    return rc;
}

int app_run(app_ctx_t *app)
{
    if (!app || !app->cfg_file) return 1;
    g_app = app;

    // signals
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;  sigaction(SIGINT,  &sa, NULL);
    sa.sa_handler = on_sigterm; sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = on_sighup;  sigaction(SIGHUP,  &sa, NULL);

    int rc = 0;
    uint64_t watchdog_usec = 0;
    uint64_t watchdog_elapsed = 0;
    int watchdog_active = sdw_watchdog_enabled(&watchdog_usec) > 0;
    config_t cfg;
    gw_bridge_runtime_t *running = NULL;
    size_t running_count = 0;
    metrics_state_t metrics;
    metrics_server_t metrics_server = {0};

    if (config_load(app->cfg_file, app->cfg_dir, &cfg) != 0) {
        fprintf(stderr, "failed to load config: %s\n", app->cfg_file);
        return 1;
    }

    const char *topic_prefix = "ingest";
    if (start_all_bridges(&cfg, topic_prefix, &running, &running_count) != 0) {
        stop_all_bridges(running, running_count);
        config_free(&cfg);
        return 1;
    }

    if (metrics_state_init(&metrics) != 0) {
        stop_all_bridges(running, running_count);
        config_free(&cfg);
        return 1;
    }
    if (refresh_metrics(&metrics, running, running_count) != 0) {
        metrics_state_destroy(&metrics);
        stop_all_bridges(running, running_count);
        config_free(&cfg);
        return 1;
    }
    if (cfg.gateway.metrics_port_set &&
        metrics_server_start(&metrics_server, cfg.gateway.metrics_port, &metrics) != 0) {
        fprintf(stderr, "failed to start metrics server on port %d\n",
                cfg.gateway.metrics_port);
        metrics_state_destroy(&metrics);
        stop_all_bridges(running, running_count);
        config_free(&cfg);
        return 1;
    }

    sdw_notify_ready();

    if (running_count == 0) {
        fprintf(stderr, "No bridges started. Waiting for signals (Ctrl+C to exit)...\n");
    } else {
        fprintf(stdout, "Gateway running with %zu bridge(s). SIGINT/SIGTERM to stop, SIGHUP to reload.\n",
                running_count);
    }

    // Service loop
    while (!app->stop) {
        if (app->reload) {
            app->reload = 0;
            fprintf(stdout, "Reloading configuration...\n");
            config_t candidate;
            gw_bridge_runtime_t *candidate_running = NULL;
            size_t candidate_count = 0;

            /* Parse the candidate before disturbing the currently working set. */
            if (config_load(app->cfg_file, app->cfg_dir, &candidate) != 0) {
                fprintf(stderr, "Reload rejected: keeping the current configuration.\n");
            } else {
                gw_bridge_runtime_t *old_running = running;
                size_t old_count = running_count;
                (void)metrics_state_replace(&metrics, NULL, 0);
                stop_all_bridges(old_running, old_count);
                running = NULL;
                running_count = 0;

                int candidate_ok = start_all_bridges(&candidate, topic_prefix,
                                                     &candidate_running,
                                                     &candidate_count) == 0 &&
                                   candidate_count > 0;
                int candidate_port = candidate.gateway.metrics_port_set
                                   ? candidate.gateway.metrics_port : 0;
                metrics_server_t replacement_server = {0};
                if (candidate_ok && candidate_port != metrics_server.port &&
                    candidate_port > 0 &&
                    metrics_server_start(&replacement_server, candidate_port,
                                         &metrics) != 0) {
                    fprintf(stderr, "Reload rejected: cannot bind metrics port %d.\n",
                            candidate_port);
                    candidate_ok = 0;
                }

                if (candidate_ok) {
                    if (candidate_port != metrics_server.port) {
                        metrics_server_stop(&metrics_server);
                        metrics_server = replacement_server;
                    }
                    config_free(&cfg);
                    cfg = candidate;
                    running = candidate_running;
                    running_count = candidate_count;
                    (void)refresh_metrics(&metrics, running, running_count);
                    fprintf(stdout, "Reload complete: %zu bridge(s) running.\n",
                            running_count);
                } else {
                    stop_all_bridges(candidate_running, candidate_count);
                    config_free(&candidate);
                    fprintf(stderr, "Reload failed: restoring the previous configuration.\n");
                    if (start_all_bridges(&cfg, topic_prefix,
                                          &running, &running_count) != 0 ||
                        running_count == 0) {
                        fprintf(stderr, "Failed to restore the previous configuration.\n");
                        rc = 1;
                        break;
                    }
                    (void)refresh_metrics(&metrics, running, running_count);
                }
            }
        }
        (void)refresh_metrics(&metrics, running, running_count);
        sleep(1);
        if (watchdog_active) {
            watchdog_elapsed += 1000000ULL;
            if (watchdog_elapsed >= watchdog_usec / 2) {
                sdw_notify_watchdog();
                watchdog_elapsed = 0;
            }
        }
    }

    sdw_notify_stopping();
    metrics_server_stop(&metrics_server);
    metrics_state_destroy(&metrics);
    stop_all_bridges(running, running_count);
    config_free(&cfg);
    return rc;
}
