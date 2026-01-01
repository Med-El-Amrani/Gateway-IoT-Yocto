#include "metrics_server.h"

#include <stdlib.h>
#include <string.h>

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static enum MHD_Result respond(struct MHD_Connection *connection,
                               unsigned int status, char *body,
                               const char *content_type) {
    if (!body) return MHD_NO;
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(body), body, MHD_RESPMEM_MUST_FREE);
    if (!response) { free(body); return MHD_NO; }
    (void)MHD_add_response_header(response, "Content-Type", content_type);
    enum MHD_Result result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result handle_request(void *cls,
                                      struct MHD_Connection *connection,
                                      const char *url, const char *method,
                                      const char *version,
                                      const char *upload_data,
                                      size_t *upload_data_size,
                                      void **connection_state) {
    (void)version; (void)upload_data; (void)upload_data_size;
    (void)connection_state;
    metrics_server_t *server = cls;
    if (strcmp(method, "GET") != 0)
        return respond(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                       copy_text("GET only\n"), "text/plain");
    if (strcmp(url, "/metrics") == 0)
        return respond(connection, MHD_HTTP_OK,
                       metrics_render_prometheus(server->state),
                       "text/plain; version=0.0.4; charset=utf-8");
    if (strcmp(url, "/health") == 0) {
        int healthy = metrics_state_healthy(server->state);
        return respond(connection, healthy ? MHD_HTTP_OK : MHD_HTTP_SERVICE_UNAVAILABLE,
                       metrics_render_health(server->state), "application/json");
    }
    return respond(connection, MHD_HTTP_NOT_FOUND,
                   copy_text("not found\n"), "text/plain");
}

int metrics_server_start(metrics_server_t *server, int port, metrics_state_t *state) {
    if (!server || !state || port < 1 || port > 65535) return -1;
    memset(server, 0, sizeof(*server));
    server->state = state;
    server->port = port;
    server->daemon = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD,
                                      (uint16_t)port, NULL, NULL,
                                      &handle_request, server, MHD_OPTION_END);
    return server->daemon ? 0 : -1;
}

void metrics_server_stop(metrics_server_t *server) {
    if (!server || !server->daemon) return;
    MHD_stop_daemon(server->daemon);
    memset(server, 0, sizeof(*server));
}
