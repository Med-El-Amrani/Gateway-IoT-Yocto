// src/app.c
#include "app.h"
#include "bridge.h"
#include "config_loader.h"
#include "config_types.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    for (size_t i = 0; i < cfg->bridges.count; ++i) {
        const bridge_t *br = &cfg->bridges.items[i];

        if (prepare_bridge_runtime_t(cfg, topic_prefix, br->name, br->from, br->to, &arr[n]) != 0) {
            fprintf(stderr, "[%s] prepare failed (from:%s to:%s)\n", br->name, br->from, br->to);
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
        }
    }

    *out_arr = arr;
    *out_cnt = n;
    return 0;
}

static void stop_all_bridges(gw_bridge_runtime_t *arr, size_t n){
    if (!arr) return;
    for (size_t i = 0; i < n; ++i) gw_bridge_stop(&arr[i]);
    free(arr);
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
    config_t cfg;
    gw_bridge_runtime_t *running = NULL;
    size_t running_count = 0;

load_and_run:
    if (config_load_file(app->cfg_file, &cfg) != 0) {
        fprintf(stderr, "failed to load config: %s\n", app->cfg_file);
        return 1;
    }

    const char *topic_prefix = "ingest";
    if (start_all_bridges(&cfg, topic_prefix, &running, &running_count) != 0) {
        config_free(&cfg);
        return 1;
    }

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
            stop_all_bridges(running, running_count);
            config_free(&cfg);
            running = NULL; running_count = 0;
            goto load_and_run;
        }
        sleep(1);
    }

    stop_all_bridges(running, running_count);
    config_free(&cfg);
    return rc;
}
