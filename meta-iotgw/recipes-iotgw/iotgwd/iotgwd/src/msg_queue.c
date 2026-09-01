#include "msg_queue.h"

#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *value) {
    if (!value) return NULL;
    size_t size = strlen(value) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, value, size);
    return copy;
}

static void clear_item(queued_msg_t *item) {
    if (!item) return;
    free(item->data);
    free(item->topic);
    free(item->content_type);
    memset(item, 0, sizeof(*item));
}

static int copy_item(queued_msg_t *item, const gw_msg_t *msg) {
    memset(item, 0, sizeof(*item));
    item->msg = *msg;
    if (msg->pl.len > 0) {
        if (!msg->pl.data) return -1;
        item->data = malloc(msg->pl.len);
        if (!item->data) return -1;
        memcpy(item->data, msg->pl.data, msg->pl.len);
    }
    item->topic = copy_string(msg->pl.topic);
    item->content_type = copy_string(msg->pl.content_type);
    if ((msg->pl.topic && !item->topic) ||
        (msg->pl.content_type && !item->content_type)) {
        clear_item(item);
        return -1;
    }
    item->msg.pl.data = item->data;
    item->msg.pl.topic = item->topic;
    item->msg.pl.content_type = item->content_type;
    return 0;
}

int msg_queue_init(msg_queue_t *queue, size_t capacity, msg_queue_policy_t policy) {
    if (!queue) return -1;
    memset(queue, 0, sizeof(*queue));
    queue->policy = policy;
    queue->capacity = capacity;
    if (capacity > 0) {
        queue->items = calloc(capacity, sizeof(*queue->items));
        if (!queue->items) return -1;
    }
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        free(queue->items);
        memset(queue, 0, sizeof(*queue));
        return -1;
    }
    queue->initialized = 1;
    return 0;
}

void msg_queue_destroy(msg_queue_t *queue) {
    if (!queue || !queue->initialized) return;
    pthread_mutex_lock(&queue->lock);
    for (size_t i = 0; i < queue->capacity; ++i) clear_item(&queue->items[i]);
    free(queue->items);
    queue->items = NULL;
    queue->capacity = queue->count = queue->head = 0;
    pthread_mutex_unlock(&queue->lock);
    pthread_mutex_destroy(&queue->lock);
    queue->initialized = 0;
}

int msg_queue_push(msg_queue_t *queue, const gw_msg_t *msg) {
    if (!queue || !queue->initialized || !msg) return -1;
    pthread_mutex_lock(&queue->lock);
    if (queue->capacity == 0) {
        queue->dropped++;
        pthread_mutex_unlock(&queue->lock);
        return 1;
    }
    if (queue->count == queue->capacity) {
        queue->dropped++;
        if (queue->policy == MSG_QUEUE_DROP_NEW) {
            pthread_mutex_unlock(&queue->lock);
            return 1;
        }
        clear_item(&queue->items[queue->head]);
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;
    }
    size_t tail = (queue->head + queue->count) % queue->capacity;
    int rc = copy_item(&queue->items[tail], msg);
    if (rc == 0) queue->count++;
    pthread_mutex_unlock(&queue->lock);
    return rc;
}

int msg_queue_flush_one(msg_queue_t *queue, msg_queue_consumer_fn consume, void *ctx) {
    if (!queue || !queue->initialized || !consume) return -1;
    pthread_mutex_lock(&queue->lock);
    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return 0;
    }
    queued_msg_t *item = &queue->items[queue->head];
    int rc = consume(ctx, &item->msg);
    if (rc == 0) {
        clear_item(item);
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;
        rc = 1;
    }
    pthread_mutex_unlock(&queue->lock);
    return rc;
}

size_t msg_queue_size(msg_queue_t *queue) {
    if (!queue || !queue->initialized) return 0;
    pthread_mutex_lock(&queue->lock);
    size_t count = queue->count;
    pthread_mutex_unlock(&queue->lock);
    return count;
}

uint64_t msg_queue_dropped(msg_queue_t *queue) {
    if (!queue || !queue->initialized) return 0;
    pthread_mutex_lock(&queue->lock);
    uint64_t dropped = queue->dropped;
    pthread_mutex_unlock(&queue->lock);
    return dropped;
}

