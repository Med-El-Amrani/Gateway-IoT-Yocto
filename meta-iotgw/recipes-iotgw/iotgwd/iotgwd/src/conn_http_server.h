#pragma once
/**
 * @file conn_http_server.h
 * @brief Connecteur HTTP serveur (réception POST) avec callback RX.
 *
 * Ce connecteur N'ENVOIE RIEN par lui-même (pas de dépendance MQTT).
 * Il appelle un callback utilisateur à la fin d'une requête POST complète.
 */

#include <stddef.h>
#include <microhttpd.h>
#include "config_types.h"   /* http_server_connector_t */
#include "bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Callback RX : retourner 0 = OK → HTTP 200 ; sinon → HTTP 500 */
typedef int (*http_rx_cb)(const char* url, const void* body, size_t len, void* user);

/** Runtime du connecteur HTTP serveur */
typedef struct {
    struct MHD_Daemon* d;
    const http_server_connector_t* cfg; /* vue sur la conf YAML */
    http_rx_cb on_rx;
    void* on_rx_user;
    int port;
} http_server_runtime_t;

/**
 * @brief Démarre le serveur HTTP d'après la configuration.
 * - écoute sur cfg->params.bind (ex: "0.0.0.0:8081", "8081")
 * - accepte toutes les routes si cfg->params.routes_count == 0
 */
int conn_http_server_start_from_config(const http_server_connector_t* cfg,
                                       http_server_runtime_t* rt);

/** @brief Enregistre le callback RX (appelé à la fin d'un POST). */
int conn_http_server_set_rx_cb(http_server_runtime_t* rt, http_rx_cb cb, void* user);

/** @brief Arrête le serveur HTTP. */
void conn_http_server_stop(http_server_runtime_t* rt);


/* Normalize an HTTP request to gw_msg_t (KIND_HTTP). */
int http_normalize(const char* url, const void* body, size_t len, gw_msg_t* out);

/* Generic RX: normalize -> transform (if set) -> send (must be set by bridge). */
int on_http_rx(const char* url, const void* body, size_t len, void* user);

#ifdef __cplusplus
}
#endif
