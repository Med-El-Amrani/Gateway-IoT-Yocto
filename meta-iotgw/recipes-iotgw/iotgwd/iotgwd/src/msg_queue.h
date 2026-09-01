#pragma once

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "gw_msg.h"

typedef enum {
    MSG_QUEUE_DROP_OLDEST = 0,
    MSG_QUEUE_DROP_NEW = 1,
} msg_queue_policy_t;

typedef struct {
    gw_msg_t msg;
    uint8_t *data;
    char *topic;
    char *content_type;
} queued_msg_t;

typedef struct {
    queued_msg_t *items;
    size_t capacity;
    size_t head;
    size_t count;
    msg_queue_policy_t policy;
    uint64_t dropped;
    pthread_mutex_t lock;
    int initialized;
} msg_queue_t;

typedef int (*msg_queue_consumer_fn)(void *ctx, const gw_msg_t *msg);

int msg_queue_init(msg_queue_t *queue, size_t capacity, msg_queue_policy_t policy);
void msg_queue_destroy(msg_queue_t *queue);
int msg_queue_push(msg_queue_t *queue, const gw_msg_t *msg);
int msg_queue_flush_one(msg_queue_t *queue, msg_queue_consumer_fn consume, void *ctx);
size_t msg_queue_size(msg_queue_t *queue);
uint64_t msg_queue_dropped(msg_queue_t *queue);

