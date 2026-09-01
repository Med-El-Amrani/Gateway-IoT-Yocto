#pragma once
#include <mosquitto.h>
#include <stdatomic.h>
#include "connectors.h"   // contient mqtt_connector_t
#include "gw_msg.h"
#include "log.h"
#include "msg_queue.h"





/* Callback message utilisateur: (topic, payload, payloadlen, user) */
typedef void (*mqtt_msg_cb)(
    const char* topic, const void* payload, int payloadlen, void* user);

typedef struct {
    struct mosquitto *mosq;
    atomic_int connected;
    atomic_int closing;
    mqtt_msg_cb on_msg;
    void *on_msg_user;
    const mqtt_connector_t *cfg;
    msg_queue_t outbound;
    atomic_uint_fast64_t published;
    atomic_uint_fast64_t publish_failures;
} mqtt_runtime_t;

/* Initialise, configure et lance le loop thread. Retour 0 = OK. */
int mqtt_connect_from_config(const mqtt_connector_t* cfg,
                             mqtt_runtime_t* rt,
                             mqtt_msg_cb on_msg,
                             void* user,
                             size_t queue_capacity,
                             msg_queue_policy_t queue_policy);

/* Publier un texte (UTF-8) avec QoS/retain. Retour 0 = OK. */
int mqtt_publish_text(mqtt_runtime_t* rt,
                      const char* topic,
                      const char* payload,
                      int qos,
                      bool retain);

/* S’arrête proprement. */
void mqtt_close(mqtt_runtime_t* rt);


int mqtt_send_adapter(void* ctx, const gw_msg_t* msg);
int mqtt_is_connected(const mqtt_runtime_t *rt);
size_t mqtt_queued_messages(mqtt_runtime_t *rt);
uint64_t mqtt_dropped_messages(mqtt_runtime_t *rt);


/* Callback de debug pour MQTT RX (si souscriptions un jour) */
void on_mqtt_msg(const char* topic, const void* payload, int len, void* user);
