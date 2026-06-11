#pragma once

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t len;
    int is_text;
    uint64_t sequence;
} websocket_history_item_t;

typedef struct {
    websocket_history_item_t *items;
    size_t capacity;
    size_t start;
    size_t count;
    uint64_t next_sequence;
    pthread_mutex_t mutex;
} websocket_history_t;

int websocket_history_init(websocket_history_t *history, size_t capacity);
void websocket_history_destroy(websocket_history_t *history);
int websocket_history_push(websocket_history_t *history, const uint8_t *data,
                           size_t len, int is_text);
uint64_t websocket_history_next_sequence(websocket_history_t *history);

/* Returns 0 with an allocated payload, 1 when caught up, or -1 on error.
 * A lagging cursor is advanced to the oldest retained message and `missed`
 * reports how many overwritten messages that client lost. */
int websocket_history_copy_next(websocket_history_t *history, uint64_t *cursor,
                                uint8_t **data, size_t *len, int *is_text,
                                uint64_t *missed);
