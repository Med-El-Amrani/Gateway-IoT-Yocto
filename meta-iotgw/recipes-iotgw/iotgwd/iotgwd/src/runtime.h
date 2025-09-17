// src/runtime.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "connector.h"
#include "bridge.h"

// Limites raisonnables pour un premier jet (peuvent être ajustées)
#ifndef IOTGW_MAX_CONNECTORS
#define IOTGW_MAX_CONNECTORS 16
#endif

#ifndef IOTGW_MAX_BRIDGES
#define IOTGW_MAX_BRIDGES    32
#endif

/**
 * @brief État runtime de la passerelle : connecteurs + bridges actifs.
 *
 * NOTE : Les champs 'const char *' dans connector_t / bridge_t pointent vers
 *        de la mémoire fournie par le chargeur de config. 
 */
typedef struct runtime_cfg {
    connector_t connectors[IOTGW_MAX_CONNECTORS];
    size_t      n_connectors;

    bridge_t    bridges[IOTGW_MAX_BRIDGES];
    size_t      n_bridges;

    // Compteurs/observabilité de base
    size_t      files_loaded;
    size_t      fragments_loaded;
} runtime_cfg_t;

/**
 * @brief Met l'état runtime à zéro (ne libère rien dynamiquement).
 */
void runtime_reset(runtime_cfg_t *rt);

/**
 * @brief Ajoute un connecteur (copie par valeur).
 * @param rt   runtime
 * @param src  modèle (déjà rempli par le loader)
 * @return pointeur vers l’élément stocké, ou NULL si plein.
 */
connector_t *runtime_add_connector(runtime_cfg_t *rt, const connector_t *src);

/**
 * @brief Retrouve un connecteur par son id exact.
 * @param rt  runtime
 * @param id  identifiant (ex: "mqtt_local")
 * @return pointeur sur le connecteur, ou NULL si absent.
 */
connector_t *runtime_find_connector(runtime_cfg_t *rt, const char *id);

/**
 * @brief Ajoute un bridge (copie par valeur).
 * @param rt   runtime
 * @param src  modèle (from/to déjà résolus si possible)
 * @return pointeur vers l’élément stocké, ou NULL si plein.
 */
bridge_t *runtime_add_bridge(runtime_cfg_t *rt, const bridge_t *src);

#ifdef __cplusplus
}
#endif
