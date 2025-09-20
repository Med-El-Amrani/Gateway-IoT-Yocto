#pragma once
/**
 * @file bridge.h
 * @brief API publique d’un bridge générique (source → destination).
 *
 * Implémentation actuelle : supporte HTTP(server) → MQTT.
 * Les autres couples (Modbus, CAN, …) pourront être ajoutés dans bridge.c
 * sans impacter le main ni les autres modules.
 */

#include "config_types.h"        // connector_any_t, enums CONN_KIND_*
#include "conn_mqtt.h"           // mqtt_runtime_t (utilisé si dest = MQTT)
#include "conn_http_server.h"    // http_server_runtime_t (utilisé si src = HTTP server)
#include "gw_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct gw_bridge_runtime_t
 * @brief Contexte runtime pour un bridge en cours d’exécution.
 *
 * - from/to : connecteurs YAML résolus (source/destination)
 * - mqtt_rt : runtime MQTT (utilisé si 'to' == MQTT)
 * - http_rt : runtime HTTP server (utilisé si 'from' == HTTP server)
 */
typedef struct {
    char id[128];
    const connector_any_t* from;
    const connector_any_t* to;

    mqtt_runtime_t        mqtt_rt;  /**< utilisé si destination = MQTT       */
    http_server_runtime_t http_rt;  /**< utilisé si source = HTTP(server)    */
    char topic_prefix[128]; 
     // ---- Générique ----
    gw_transform_fn transform;   // ex: http_to_mqtt (par défaut si NULL)
    void*           transform_user;
    gw_send_fn      send_fn;     // ex: mqtt_send_adapter
    void*           send_ctx;    // ex: &mqtt_rt
} gw_bridge_runtime_t;

/**
 * @brief Démarre un bridge générique `from → to`.
 *
 * Étapes :
 *   1) Prépare/ouvre la destination (ex: connexion MQTT)
 *   2) Démarre la source (ex: HTTP server) et câble le pont
 *
 * @param from         connecteur source (p.ex. http-server)
 * @param to           connecteur destination (p.ex. mqtt)
 * @param bridge_id    identifiant du bridge (pour logs)
 * @param topic_prefix préfixe MQTT si applicable (ex: "ingest"), peut être NULL
 * @param out          runtime peuplé si succès
 * @return 0 = OK, -1 = erreur d’init/connexion, -2 = couple non supporté
 */
int gw_bridge_start(const connector_any_t* from,
                    const connector_any_t* to,
                    const char* bridge_id,
                    const char* topic_prefix,
                    gw_bridge_runtime_t* out);

/**
 * @brief Arrête proprement un bridge démarré par gw_bridge_start().
 */
void gw_bridge_stop(gw_bridge_runtime_t* b);

#ifdef __cplusplus
}
#endif
