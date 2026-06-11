#pragma once

#include "connectors.h"
#include "gw_msg.h"

typedef struct websocket_shared_server websocket_shared_server_t;

typedef struct {
    websocket_shared_server_t *server;
} websocket_destination_t;

int websocket_destination_start(const websocket_server_connector_t *cfg,
                                websocket_destination_t *destination);
void websocket_destination_stop(websocket_destination_t *destination);
int websocket_send_adapter(void *ctx, const gw_msg_t *message);
