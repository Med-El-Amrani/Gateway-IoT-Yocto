#pragma once
#include <mosquitto.h>
#include "connectors.h"   // contient mqtt_connector_t




typedef struct {
    struct mosquitto *mosq;
    int connected;
} mqtt_runtime_t;

/* Callback message utilisateur: (topic, payload, payloadlen, user) */
typedef void (*mqtt_msg_cb)(
    const char* topic, const void* payload, int payloadlen, void* user);

/* Initialise, configure et lance le loop thread. Retour 0 = OK. */
int mqtt_connect_from_config(const mqtt_connector_t* cfg,
                             mqtt_runtime_t* rt,
                             mqtt_msg_cb on_msg,
                             void* user);

/* Publier un texte (UTF-8) avec QoS/retain. Retour 0 = OK. */
int mqtt_publish_text(mqtt_runtime_t* rt,
                      const char* topic,
                      const char* payload,
                      int qos,
                      bool retain);

/* S’arrête proprement. */
void mqtt_close(mqtt_runtime_t* rt);
