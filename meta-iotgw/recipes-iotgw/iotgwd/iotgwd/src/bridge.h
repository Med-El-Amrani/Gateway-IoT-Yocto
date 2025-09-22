#pragma once
/**
 * @file bridge.h
 * @brief API publique d’un bridge générique (source → destination).
 *
 * Implémentation actuelle : supporte HTTP(server) → MQTT.
 * Les autres couples (Modbus, CAN, …) pourront être ajoutés dans bridge.c
 * sans impacter le main ni les autres modules.
 */

#include "config_types.h"        // connector_any_t, enums KIND_*
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
    const connector_any_t* from;   // source connector (config)
    const connector_any_t* to;     // destination connector (config)

    char topic_prefix[128];

    // Opaque runtime contexts (allocated/owned by start/stop code)
    void* source_ctx;              // e.g. http_server_runtime_t*
    void* dest_ctx;                // e.g. mqtt_runtime_t*

    // Transform + Send hooks
    gw_transform_fn transform;     // e.g. http_to_mqtt_default (NULL => default)
    void*           transform_user;

    gw_send_fn      send_fn;       // e.g. mqtt_send_adapter
    void*           send_ctx;      // usually == dest_ctx
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
int gw_bridge_start(gw_bridge_runtime_t* out);

/**
 * @brief Arrête proprement un bridge démarré par gw_bridge_start().
 */
int gw_bridge_stop(gw_bridge_runtime_t* b);



/* Prepare runtime from config: resolve connectors, fill ids/prefix, pick defaults.
 * protocol_src / protocol_dst are CONNECTOR NAMES from YAML (e.g. "http1", "mqtt1"). */
int prepare_bridge_runtime_t(const config_t* cfg,
                             const char* topic_prefix,
                             const char* bridge_id,
                             const char* protocol_src,
                             const char* protocol_dst,
                             gw_bridge_runtime_t* out);

#ifdef __cplusplus
}
#endif
