#include "websocket_history.h"

#include <stdlib.h>
#include <string.h>

int websocket_history_init(websocket_history_t *history, size_t capacity) {
    if (!history || capacity == 0) return -1;
    memset(history, 0, sizeof(*history));
    history->items = calloc(capacity, sizeof(*history->items));
    if (!history->items) return -1;
    history->capacity = capacity;
    history->next_sequence = 1;
    if (pthread_mutex_init(&history->mutex, NULL) != 0) {
        free(history->items);
        memset(history, 0, sizeof(*history));
        return -1;
    }
    return 0;
}

void websocket_history_destroy(websocket_history_t *history) {
    if (!history || !history->items) return;
    pthread_mutex_lock(&history->mutex);
    for (size_t i = 0; i < history->capacity; ++i) free(history->items[i].data);
    free(history->items);
    history->items = NULL;
    pthread_mutex_unlock(&history->mutex);
    pthread_mutex_destroy(&history->mutex);
    memset(history, 0, sizeof(*history));
}

int websocket_history_push(websocket_history_t *history, const uint8_t *data,
                           size_t len, int is_text) {
    if (!history || !history->items || !data || len == 0) return -1;
    uint8_t *copy = malloc(len);
    if (!copy) return -1;
    memcpy(copy, data, len);

    pthread_mutex_lock(&history->mutex);
    size_t index;
    if (history->count == history->capacity) {
        index = history->start;
        history->start = (history->start + 1) % history->capacity;
        free(history->items[index].data);
    } else {
        index = (history->start + history->count) % history->capacity;
        history->count++;
    }
    history->items[index] = (websocket_history_item_t){
        .data = copy, .len = len, .is_text = !!is_text,
        .sequence = history->next_sequence++
    };
    pthread_mutex_unlock(&history->mutex);
    return 0;
}

uint64_t websocket_history_next_sequence(websocket_history_t *history) {
    if (!history || !history->items) return 0;
    pthread_mutex_lock(&history->mutex);
    uint64_t next = history->next_sequence;
    pthread_mutex_unlock(&history->mutex);
    return next;
}

int websocket_history_copy_next(websocket_history_t *history, uint64_t *cursor,
                                uint8_t **data, size_t *len, int *is_text,
                                uint64_t *missed) {
    if (!history || !history->items || !cursor || !data || !len || !is_text)
        return -1;
    *data = NULL;
    if (missed) *missed = 0;

    pthread_mutex_lock(&history->mutex);
    uint64_t oldest = history->count
                    ? history->items[history->start].sequence
                    : history->next_sequence;
    if (*cursor < oldest) {
        if (missed) *missed = oldest - *cursor;
        *cursor = oldest;
    }
    if (*cursor >= history->next_sequence) {
        pthread_mutex_unlock(&history->mutex);
        return 1;
    }
    size_t offset = (size_t)(*cursor - oldest);
    if (offset >= history->count) {
        pthread_mutex_unlock(&history->mutex);
        return 1;
    }
    websocket_history_item_t *item =
        &history->items[(history->start + offset) % history->capacity];
    uint8_t *copy = malloc(item->len);
    if (!copy) {
        pthread_mutex_unlock(&history->mutex);
        return -1;
    }
    memcpy(copy, item->data, item->len);
    *data = copy;
    *len = item->len;
    *is_text = item->is_text;
    *cursor = item->sequence + 1;
    pthread_mutex_unlock(&history->mutex);
    return 0;
}
