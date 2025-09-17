#pragma once
#include <stddef.h>
#include "connector.h"

/* Configuration de base pour le connecteur MQTT */
typedef struct {
    const char  *host;         /* ex: "127.0.0.1" */
    int          port;         /* ex: 1883 */
    const char  *client_id;    /* ex: "iotgw-rpi4" (optionnel) */
    int          keepalive;    /* ex: 30s, défaut 30 si 0 */
    int          qos;          /* 0/1/2, défaut 0 */
    int          retain;       /* 0/1, défaut 0 */
    const char  *username;     /* optionnel */
    const char  *password;     /* optionnel */

    /* Abonnements initiaux (lecture côté gateway).
       Si tu laisses NULL/0, aucun abonnement par défaut. */
    const char *const *sub_topics;
    size_t      n_sub_topics;
} conn_mqtt_cfg_t;

/* Fabrique un connector_t de type MQTT et tente de se connecter. 
   Retourne NULL en cas d’erreur d’init (lib, socket…). */
connector_t *conn_mqtt_create(const char *id, const conn_mqtt_cfg_t *cfg);

/* Détruit le connecteur + libère ressources. */
void conn_mqtt_destroy(connector_t *c);
