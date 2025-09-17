#include "app.h"
#include "runtime.h"
#include "config_loader.h"
#include "sdwrap.h"
#include "log.h"
#include "connector.h"
#include "bridge.h"

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

    // cadence du scheduler (100 ms recommandé pour SPI<->MQTT)
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
    uint64_t elapsed_ns = 0;
    uint64_t wd_interval_ns = wd_on ? (wd_usec * 3 / 5) * 1000 : 0; // ~60% marge

    // main loop (placeholder)
    while(!app->stop){
             // 1) Faire “vivre” chaque connecteur (SPI: transferts/queues ; MQTT: boucle réseau)
        connectors_poll(rt.connectors, rt.n_connectors);

        // 2) Appliquer les règles de bridge (ex: SPI -> MQTT, MQTT -> SPI, etc.)
        bridges_tick(rt.bridges, rt.n_bridges);

        // 3) Tick + watchdog
        nanosleep(&ts, NULL);
        if(wd_on){
            elapsed_ns += 100ULL * 1000ULL * 1000ULL; // +100 ms
            if(elapsed_ns >= wd_interval_ns){
                sdw_notify_watchdog();
                elapsed_ns = 0;
            }
        }

        // 4) Reload config à la volée
        if(app->reload){
            log_info("SIGHUP: reloading configs");
            runtime_cfg_t newrt;
            if(config_load_all(&newrt, app->cfg_file, app->cfg_dir) == 0){
                // TODO plus tard : fermer proprement les anciens connecteurs si nécessaire
                rt = newrt;
                log_info("Reload OK");
            } else {
                log_err("Reload FAILED (keeping previous runtime state)");
            }
            app->reload = 0;
        }
    }

    sdw_notify_stopping();
    log_info("Stopping daemon");
    return 0;
}

