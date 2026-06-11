#include "conn_websocket_server.h"

#include <libwebsockets.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "websocket_history.h"

typedef struct {
    uint64_t cursor;
    int active;
} websocket_session_t;

struct websocket_shared_server {
    char *bind;
    char *path;
    int port;
    int max_clients;
    int clients;
    int running;
    int broadcast_pending;
    size_t references;
    pthread_t thread;
    pthread_mutex_t mutex;
    struct lws_context *context;
    websocket_history_t history;
    struct websocket_shared_server *next;
};

static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static websocket_shared_server_t *servers;

static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len);

static const struct lws_protocols websocket_protocols[] = {
    { "iotgw", websocket_callback, sizeof(websocket_session_t), 64 * 1024, 0, NULL, 0 },
    LWS_PROTOCOL_LIST_TERM
};

static websocket_shared_server_t *server_from_wsi(struct lws *wsi) {
    return lws_context_user(lws_get_context(wsi));
}

static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    (void)in;
    (void)len;
    websocket_shared_server_t *server = server_from_wsi(wsi);
    websocket_session_t *session = user;
    if (!server) return -1;

    switch (reason) {
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
        char uri[256];
        int copied = lws_hdr_copy(wsi, uri, sizeof(uri), WSI_TOKEN_GET_URI);
        if (copied <= 0 || strcmp(uri, server->path) != 0) return 1;
        break;
    }
    case LWS_CALLBACK_ESTABLISHED:
        pthread_mutex_lock(&server->mutex);
        if (server->clients >= server->max_clients) {
            pthread_mutex_unlock(&server->mutex);
            return -1;
        }
        server->clients++;
        session->active = 1;
        pthread_mutex_unlock(&server->mutex);
        session->cursor = websocket_history_next_sequence(&server->history);
        break;
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        uint8_t *payload = NULL;
        size_t payload_len = 0;
        int is_text = 0;
        uint64_t missed = 0;
        int rc = websocket_history_copy_next(&server->history, &session->cursor,
                                             &payload, &payload_len, &is_text,
                                             &missed);
        if (missed)
            fprintf(stderr, "[websocket] slow client dropped %llu message(s)\n",
                    (unsigned long long)missed);
        if (rc == 0) {
            uint8_t *frame = malloc(LWS_PRE + payload_len);
            if (!frame) { free(payload); return -1; }
            memcpy(frame + LWS_PRE, payload, payload_len);
            int written = lws_write(wsi, frame + LWS_PRE, payload_len,
                                    is_text ? LWS_WRITE_TEXT : LWS_WRITE_BINARY);
            free(frame);
            free(payload);
            if (written < 0 || (size_t)written != payload_len) return -1;
            lws_callback_on_writable(wsi);
        }
        break;
    }
    case LWS_CALLBACK_CLOSED:
        if (session->active) {
            pthread_mutex_lock(&server->mutex);
            if (server->clients > 0) server->clients--;
            pthread_mutex_unlock(&server->mutex);
            session->active = 0;
        }
        break;
    default:
        break;
    }
    return 0;
}

static void *websocket_service_main(void *arg) {
    websocket_shared_server_t *server = arg;
    for (;;) {
        pthread_mutex_lock(&server->mutex);
        int running = server->running;
        int broadcast = server->broadcast_pending;
        server->broadcast_pending = 0;
        pthread_mutex_unlock(&server->mutex);
        if (!running) break;
        if (broadcast)
            lws_callback_on_writable_all_protocol(server->context,
                                                  &websocket_protocols[0]);
        lws_service(server->context, 250);
    }
    return NULL;
}

static int same_endpoint(const websocket_shared_server_t *server,
                         const websocket_server_params_t *params) {
    return server->port == params->port &&
           strcmp(server->bind, params->bind) == 0 &&
           strcmp(server->path, params->path) == 0;
}

static websocket_shared_server_t *create_server(
        const websocket_server_params_t *params) {
    websocket_shared_server_t *server = calloc(1, sizeof(*server));
    if (!server) return NULL;
    server->bind = strdup(params->bind);
    server->path = strdup(params->path);
    server->port = params->port;
    server->max_clients = params->max_clients;
    server->references = 1;
    if (!server->bind || !server->path) {
        free(server->bind); free(server->path); free(server);
        return NULL;
    }
    if (pthread_mutex_init(&server->mutex, NULL) != 0) {
        free(server->bind); free(server->path); free(server);
        return NULL;
    }
    if (websocket_history_init(&server->history,
                               (size_t)params->history_size) != 0) {
        pthread_mutex_destroy(&server->mutex);
        free(server->bind); free(server->path); free(server);
        return NULL;
    }

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = params->port;
    info.iface = strcmp(params->bind, "0.0.0.0") == 0 ? NULL : params->bind;
    info.protocols = websocket_protocols;
    info.user = server;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    server->context = lws_create_context(&info);
    if (!server->context) {
        websocket_history_destroy(&server->history);
        pthread_mutex_destroy(&server->mutex);
        free(server->bind); free(server->path); free(server);
        return NULL;
    }
    server->running = 1;
    if (pthread_create(&server->thread, NULL, websocket_service_main, server) != 0) {
        server->running = 0;
        lws_context_destroy(server->context);
        websocket_history_destroy(&server->history);
        pthread_mutex_destroy(&server->mutex);
        free(server->bind); free(server->path); free(server);
        return NULL;
    }
    fprintf(stderr, "[websocket] listening on %s:%d%s\n",
            params->bind, params->port, params->path);
    return server;
}

int websocket_destination_start(const websocket_server_connector_t *cfg,
                                websocket_destination_t *destination) {
    if (!cfg || !destination || !cfg->params.bind || !cfg->params.path ||
        cfg->params.port <= 0 || cfg->params.max_clients <= 0 ||
        cfg->params.history_size <= 0) return -1;
    pthread_mutex_lock(&registry_mutex);
    for (websocket_shared_server_t *server = servers; server; server = server->next) {
        if (same_endpoint(server, &cfg->params)) {
            if (server->max_clients != cfg->params.max_clients ||
                server->history.capacity != (size_t)cfg->params.history_size) {
                pthread_mutex_unlock(&registry_mutex);
                fprintf(stderr, "[websocket] shared endpoint has conflicting limits\n");
                return -1;
            }
            server->references++;
            destination->server = server;
            pthread_mutex_unlock(&registry_mutex);
            return 0;
        }
    }
    websocket_shared_server_t *server = create_server(&cfg->params);
    if (!server) { pthread_mutex_unlock(&registry_mutex); return -1; }
    server->next = servers;
    servers = server;
    destination->server = server;
    pthread_mutex_unlock(&registry_mutex);
    return 0;
}

void websocket_destination_stop(websocket_destination_t *destination) {
    if (!destination || !destination->server) return;
    pthread_mutex_lock(&registry_mutex);
    websocket_shared_server_t *server = destination->server;
    destination->server = NULL;
    if (--server->references > 0) { pthread_mutex_unlock(&registry_mutex); return; }
    websocket_shared_server_t **link = &servers;
    while (*link && *link != server) link = &(*link)->next;
    if (*link) *link = server->next;
    pthread_mutex_lock(&server->mutex);
    server->running = 0;
    pthread_mutex_unlock(&server->mutex);
    lws_cancel_service(server->context);
    pthread_mutex_unlock(&registry_mutex);

    pthread_join(server->thread, NULL);
    lws_context_destroy(server->context);
    websocket_history_destroy(&server->history);
    pthread_mutex_destroy(&server->mutex);
    free(server->bind); free(server->path); free(server);
}

int websocket_send_adapter(void *ctx, const gw_msg_t *message) {
    websocket_destination_t *destination = ctx;
    if (!destination || !destination->server || !message ||
        !message->pl.data || message->pl.len == 0) return -1;
    if (websocket_history_push(&destination->server->history,
                               message->pl.data, message->pl.len,
                               message->pl.is_text) != 0) return -1;
    pthread_mutex_lock(&destination->server->mutex);
    destination->server->broadcast_pending = 1;
    pthread_mutex_unlock(&destination->server->mutex);
    lws_cancel_service(destination->server->context);
    return 0;
}
