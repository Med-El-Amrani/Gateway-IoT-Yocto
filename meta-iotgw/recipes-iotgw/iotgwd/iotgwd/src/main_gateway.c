/**
 * @file main_gateway.c
 * @brief Démo minimale de passerelle HTTP(server) → MQTT en s'appuyant
 *        sur ton chargeur YAML et tes modules de connecteurs.
 *
 * Ce binaire :
 *   1) charge un fichier YAML maître (incluant d'autres fragments via `includes:`),
 *   2) parcourt la table `bridges:`,
 *   3) démarre tous les bridges dont la source est `http-server` et la destination `mqtt`,
 *   4) publie sur MQTT chaque requête HTTP POST reçue (topic = "<prefix>/<path>").
 *
 * Entrées (arguments) :
 *   - argv[1] : chemin du fichier YAML maître (défaut: "../../files/config.example.yaml")
 *   - argv[2] : préfixe de topic MQTT (défaut: "ingest")
 *
 * Dépendances (runtime) :
 *   - Broker MQTT joignable (ex: mosquitto sur localhost:1883)
 *   - libmicrohttpd (serveur HTTP) & libmosquitto (client MQTT)
 *
 * Test rapide :
 *   1) Terminal A : ./gateway_main ../../files/config.example.yaml ingest
 *   2) Terminal B : mosquitto_sub -h localhost -t 'ingest/#' -v
 *   3) Terminal C : curl -X POST http://localhost:8081/temperature -d '23.5'
 *      → attendu côté B : "ingest/temperature 23.5"
 */
// src/main_gateway.c (local tester)
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "config_types.h"
#include "bridge.h"
#include "config_loader.h"

static volatile int g_stop = 0;
static void on_sig(int s){ (void)s; g_stop = 1; }

int main(int argc, char** argv){
    const char* cfg_path     = (argc > 1) ? argv[1] : "../../files/config.example.yaml";
    const char* topic_prefix = (argc > 2) ? argv[2] : "ingest";

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    config_t cfg;
    if (config_load_file(cfg_path, &cfg) != 0) {
        fprintf(stderr, "failed to load config: %s\n", cfg_path);
        return 1;
    }

    gw_bridge_runtime_t running[64];
    size_t running_count = 0;

    for (size_t i=0; i<cfg.bridges.count; ++i) {
        const bridge_t* br = &cfg.bridges.items[i];

        if (prepare_bridge_runtime_t(&cfg, topic_prefix, br->name, br->from, br->to,
                                     &running[running_count]) != 0) {
            fprintf(stderr, "[%s] prepare failed (from:%s to:%s)\n", br->name, br->from, br->to);
            continue;
        }
        if (!running[running_count].from || !running[running_count].to) {
            fprintf(stderr, "[%s] missing connector (from:%s to:%s)\n", br->name, br->from, br->to);
            continue;
        }
        if (gw_bridge_start(&running[running_count]) == 0) {
            running_count++;
        } else {
            printf("[bridge:%s] skip (pair %d→%d not supported yet)\n",
                   br->name, (int)running[running_count].from->kind, (int)running[running_count].to->kind);
        }
    }

    if (running_count == 0) {
        printf("No supported bridges started. Exiting.\n");
        config_free(&cfg);
        return 0;
    }

    puts("Gateway demo running. Ctrl+C to stop.");
    while (!g_stop) pause();

    for (size_t i=0; i<running_count; ++i) gw_bridge_stop(&running[i]);
    config_free(&cfg);
    return 0;
}
