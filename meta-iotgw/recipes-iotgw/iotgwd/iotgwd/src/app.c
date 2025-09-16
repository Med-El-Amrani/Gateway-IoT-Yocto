#include "app.h"
#include "runtime.h"
#include "config_loader.h"
#include "sdwrap.h"
#include "log.h"

#include <signal.h>
#include <time.h>
#include <string.h>

static app_ctx_t *g_app = 0;

static void on_sighup (int s){ (void)s; if(g_app) g_app->reload = 1; }
static void on_sigterm(int s){ (void)s; if(g_app) g_app->stop   = 1; }

int app_run(app_ctx_t *app){
    g_app = app;

    // signals
    struct sigaction sa = {0};
    sa.sa_handler = on_sighup;  sigaction(SIGHUP,  &sa, NULL);
    sa.sa_handler = on_sigterm; sigaction(SIGINT,  &sa, NULL);
                                 sigaction(SIGTERM, &sa, NULL);

    runtime_cfg_t rt;
    if(config_load_all(&rt, app->cfg_file, app->cfg_dir) != 0){
        log_err("Initial config load failed");
        return 2;
    }

    // READY=1
    sdw_notify_ready();
    log_info("iotgwd ready (main=%s confdir=%s)", app->cfg_file, app->cfg_dir);

    // watchdog
    uint64_t wd_usec = 0;
    int wd_on = sdw_watchdog_enabled(&wd_usec) > 0;
    struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
    uint64_t elapsed_ns = 0;
    uint64_t wd_interval_ns = wd_on ? (wd_usec * 3 / 5) * 1000 : 0; // ~60% margin

    // main loop (placeholder)
    while(!app->stop){
        // TODO: poll connectors, process bridges, timers, etc.

        // tick
        nanosleep(&ts, NULL);
        if(wd_on){
            elapsed_ns += 1000000000ULL;
            if(elapsed_ns >= wd_interval_ns){
                sdw_notify_watchdog();
                elapsed_ns = 0;
            }
        }

        if(app->reload){
            log_info("SIGHUP: reloading configs");
            if(config_load_all(&rt, app->cfg_file, app->cfg_dir) == 0){
                log_info("Reload OK");
            } else {
                log_err("Reload FAILED (keeping previous runtime state)");
                // NOTE: you could keep a backup of 'rt' and roll back on failure.
            }
            app->reload = 0;
        }
    }

    sdw_notify_stopping();
    log_info("Stopping daemon");
    return 0;
}
