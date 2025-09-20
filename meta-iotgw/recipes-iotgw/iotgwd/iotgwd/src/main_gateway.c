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

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include "config_types.h"
#include "bridge.h"
#include "config_loader.h"


/* --- Gestion simple des signaux pour un arrêt propre --- */
static volatile int g_stop = 0;
/** @brief Handler SIGINT/SIGTERM : demande l'arrêt de la boucle principale. */
static void on_sig(int s){ (void)s; g_stop = 1; }


/**
 * @brief Point d'entrée : charge la config, prépare et lance les bridges ciblés,
 *        puis attend SIGINT/SIGTERM pour arrêter proprement.
 *
 * Convention de sélection :
 *   - Cette démo **ne démarre que** les routes de type "http-server → mqtt".
 *     Les autres sont loguées en "skip".
 */
int main(int argc, char** argv){
    /* 1) Lecture des arguments (YAML + préfixe de topic) */
    const char* cfg_path = (argc > 1) ? argv[1] : "../../files/config.example.yaml";
    const char* topic_prefix = (argc > 2) ? argv[2] : "ingest";

    /* 2) Hook des signaux pour un arrêt contrôlé */
    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    /* 3) Chargement du YAML maître (incluant les fragments "includes:") */
    config_t cfg;
    if(config_load_file(cfg_path, &cfg)!=0){
        fprintf(stderr, "failed to load config: %s\n", cfg_path);
        return 1;
    }

    /* 4) Parcours des bridges et démarrage de ceux qui nous intéressent */
    gw_bridge_runtime_t running[64];
    size_t running_count = 0;

    for(size_t i=0; i<cfg.bridges.count; ++i){
        const bridge_t* br = &cfg.bridges.items[i];  // bridge_t de config_types.h (OK)

        running[running_count].from = config_find_connector(&cfg, br->from);
        running[running_count].to  = config_find_connector(&cfg, br->to);
        if(!running[running_count].from  || !running[running_count].to){
            fprintf(stderr, "[%s] missing connector (from:%s to:%s)\n", br->name, br->from, br->to);
            continue;
        }

        if (gw_bridge_start( br->name, topic_prefix, &running[running_count]) == 0) {
            running_count++;
        } else {
            printf("[bridge:%s] skip (pair %d→%d not supported yet)\n",
                br->name, (int)running[running_count].from->kind, (int)running[running_count].to->kind);
        }
    }

    /* 5) Si aucun bridge compatible n'a été trouvé, on sort proprement */
    if(running_count == 0){
        printf("No http-server -> mqtt bridges to run. Exiting.\n");
        config_free(&cfg);
        return 0;
    }

    /* 6) Boucle "service" : on attend un signal d'arrêt (Ctrl+C) */
    puts("Gateway demo running. Ctrl+C to stop.");
    while(!g_stop) pause();

    /* 7) Arrêt en miroir : stoppe chaque bridge puis libère la config */
    for(size_t i=0;i<running_count;i++) gw_bridge_stop(&running[i]);
    config_free(&cfg);
    return 0;
}
