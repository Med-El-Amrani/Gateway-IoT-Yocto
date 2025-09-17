#include "conn_mqtt.h"
#include <mosquitto.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* ---------- Petit cache topic -> valeur double ------------------------- */

#define MQTT_CACHE_MAX 128
#define MQTT_TOPIC_MAX 128
struct cache_entry {
    char   topic[MQTT_TOPIC_MAX];
    double value;
    int    valid;
};

static int cache_find(struct cache_entry *tab, const char *topic)
{
    for (int i = 0; i < MQTT_CACHE_MAX; ++i)
        if (tab[i].valid && strcmp(tab[i].topic, topic) == 0)
            return i;
    return -1;
}
static int cache_upsert(struct cache_entry *tab, const char *topic, double v)
{
    int idx = cache_find(tab, topic);
    if (idx < 0) {
        for (int i = 0; i < MQTT_CACHE_MAX; ++i) {
            if (!tab[i].valid) {
                strncpy(tab[i].topic, topic, MQTT_TOPIC_MAX-1);
                tab[i].topic[MQTT_TOPIC_MAX-1] = 0;
                tab[i].value = v;
                tab[i].valid = 1;
                return 0;
            }
        }
        return -ENOSPC;
    } else {
        tab[idx].value = v;
        return 0;
    }
}

/* ---------- Implantation interne --------------------------------------- */

typedef struct {
    struct mosquitto *mq;
    conn_mqtt_cfg_t   cfg;
    int               connected;
    struct cache_entry cache[MQTT_CACHE_MAX];
} mqtt_impl_t;

/* Petit refcount global pour init/cleanup de la lib mosquitto */
static int g_mosq_refcnt = 0;

static void lib_init(void)
{
    if (g_mosq_refcnt++ == 0)
        mosquitto_lib_init();
}
static void lib_cleanup(void)
{
    if (--g_mosq_refcnt == 0)
        mosquitto_lib_cleanup();
}

/* Callbacks Mosquitto */
static void on_connect(struct mosquitto *m, void *userdata, int rc)
{
    (void)m;
    mqtt_impl_t *impl = (mqtt_impl_t*)userdata;
    impl->connected = (rc == 0);
    if (!impl->connected) return;

    /* (Re)subscribe aux topics */
    for (size_t i = 0; i < impl->cfg.n_sub_topics; ++i) {
        const char *t = impl->cfg.sub_topics[i];
        if (t && *t)
            mosquitto_subscribe(impl->mq, NULL, t, impl->cfg.qos > 0 ? impl->cfg.qos : 0);
    }
}

static void on_disconnect(struct mosquitto *m, void *userdata, int rc)
{
    (void)m; (void)rc;
    mqtt_impl_t *impl = (mqtt_impl_t*)userdata;
    impl->connected = 0;
}

static void on_message(struct mosquitto *m, void *userdata,
                       const struct mosquitto_message *msg)
{
    (void)m;
    mqtt_impl_t *impl = (mqtt_impl_t*)userdata;
    if (!msg || !msg->topic || !msg->payload) return;

    /* On attend des payloads numériques; on ignore le reste. */
    char buf[256];
    size_t n = (msg->payloadlen < (int)sizeof(buf)-1) ? (size_t)msg->payloadlen : sizeof(buf)-1;
    memcpy(buf, msg->payload, n);
    buf[n] = 0;

    char *endp = NULL;
    double v = strtod(buf, &endp);
    if (endp == buf) return; /* pas de nombre -> ignore */

    cache_upsert(impl->cache, msg->topic, v);
}

/* ---------- Fonctions du connector_t ----------------------------------- */

static int mqtt_read(connector_t *c, const char *key, double *out)
{
    if (!c || !key || !out) return -EINVAL;
    mqtt_impl_t *impl = (mqtt_impl_t*)c->impl;
    int idx = cache_find(impl->cache, key);
    if (idx < 0) return -ENOENT;
    *out = impl->cache[idx].value;
    return 0;
}

static int mqtt_write(connector_t *c, const char *key, double val)
{
    if (!c || !key) return -EINVAL;
    mqtt_impl_t *impl = (mqtt_impl_t*)c->impl;
    if (!impl->connected) return -ENETDOWN;

    char payload[64];
    /* format simple; adapte si tu veux JSON etc. */
    int len = snprintf(payload, sizeof(payload), "%.6f", val);
    if (len < 0) return -EIO;

    int qos = (impl->cfg.qos > 0) ? impl->cfg.qos : 0;
    int retain = impl->cfg.retain ? 1 : 0;
    int rc = mosquitto_publish(impl->mq, NULL, key, len, payload, qos, retain);
    return (rc == MOSQ_ERR_SUCCESS) ? 0 : -EIO;
}

static void mqtt_poll(connector_t *c)
{
    if (!c) return;
    mqtt_impl_t *impl = (mqtt_impl_t*)c->impl;
    /* Non-bloquant; laisse le main loop rythmer l’IO */
    mosquitto_loop(impl->mq, 0 /* timeout ms*/, 1 /* max packets */);
}

/* ---------- API publique ----------------------------------------------- */


connector_t *conn_mqtt_create(const char *id, const conn_mqtt_cfg_t *cfg)
{
    // 1) Validation des paramètres d’entrée
    if (!cfg || !cfg->host || cfg->port <= 0) {
        errno = EINVAL;
        return NULL;
    }

    // 2) Allocation des structures
    connector_t *c = (connector_t*)calloc(1, sizeof(*c));
    mqtt_impl_t *impl = (mqtt_impl_t*)calloc(1, sizeof(*impl));
    if (!c || !impl) { free(c); free(impl); errno = ENOMEM; return NULL; }

    // 3) Copie "light" de la configuration (pointeurs conservés)
    impl->cfg = *cfg;

    // 4) Initialisation globale de la lib Mosquitto (référence partagée)
    lib_init();

    // 5) Création du client Mosquitto
    struct mosquitto *mq = mosquitto_new(
        cfg->client_id && *cfg->client_id ? cfg->client_id : NULL,
        true,   // clean session : le broker n’enregistre pas les abonnements
        impl    // userdata : permettra aux callbacks d’accéder à mqtt_impl_t
    );
    if (!mq) { lib_cleanup(); free(impl); free(c); return NULL; }

    // 6) Branche les callbacks (connexion/déconnexion/message)
    impl->mq = mq;
    mosquitto_connect_callback_set(mq, on_connect);
    mosquitto_disconnect_callback_set(mq, on_disconnect);
    mosquitto_message_callback_set(mq, on_message);

    // 7) Authentification si demandée
    if (cfg->username || cfg->password) {
        mosquitto_username_pw_set(mq, cfg->username, cfg->password);
    }

    // 8) Connexion au broker (keepalive par défaut à 30s si non fourni)
    int ka = cfg->keepalive > 0 ? cfg->keepalive : 30;
    int rc = mosquitto_connect(mq, cfg->host, cfg->port, ka);
    if (rc != MOSQ_ERR_SUCCESS) {
        // échec : nettoyage complet et errno explicite
        mosquitto_destroy(mq);
        lib_cleanup();
        free(impl);
        free(c);
        errno = ECONNREFUSED;
        return NULL;
    }

    // 9) Remplir l’objet générique connector_t (vtable + meta)
    c->id   = id ? id : "mqtt";   // ATTENTION : pas de strdup -> la chaîne doit rester valide
    c->type = CONN_MQTT;
    c->impl = impl;               // état spécifique (mqtt_impl_t) stocké en void*
    c->read = mqtt_read;          // lit la dernière valeur numérique d’un topic depuis le cache
    c->write= mqtt_write;         // publie un double vers un topic
    c->poll = mqtt_poll;          // pompe non bloquante mosquitto_loop(...)
    return c;
}


void conn_mqtt_destroy(connector_t *c)
{
    if (!c) return;
    mqtt_impl_t *impl = (mqtt_impl_t*)c->impl;
    if (impl && impl->mq) {
        mosquitto_disconnect(impl->mq);
        mosquitto_destroy(impl->mq);
    }
    lib_cleanup();
    free(impl);
    free(c);
}
