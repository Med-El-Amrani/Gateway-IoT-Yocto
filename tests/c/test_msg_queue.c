#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "msg_queue.h"

typedef struct {
    uint8_t values[8];
    size_t count;
    int fail;
} collector_t;

static int collect(void *ctx, const gw_msg_t *msg) {
    collector_t *collector = ctx;
    if (collector->fail) return -1;
    assert(msg->pl.len == 1);
    collector->values[collector->count++] = msg->pl.data[0];
    return 0;
}

static gw_msg_t message(uint8_t *value, const char *topic) {
    gw_msg_t msg = {0};
    msg.protocole = KIND_MQTT;
    msg.pl.data = value;
    msg.pl.len = 1;
    msg.pl.topic = topic;
    return msg;
}

static void test_drop_oldest_and_deep_copy(void) {
    msg_queue_t queue;
    collector_t collector = {0};
    assert(msg_queue_init(&queue, 2, MSG_QUEUE_DROP_OLDEST) == 0);

    uint8_t first = 1, second = 2, third = 3;
    gw_msg_t one = message(&first, "one");
    gw_msg_t two = message(&second, "two");
    gw_msg_t three = message(&third, "three");
    assert(msg_queue_push(&queue, &one) == 0);
    first = 99;
    assert(msg_queue_push(&queue, &two) == 0);
    assert(msg_queue_push(&queue, &three) == 0);
    assert(msg_queue_size(&queue) == 2);
    assert(msg_queue_dropped(&queue) == 1);
    assert(msg_queue_flush_one(&queue, collect, &collector) == 1);
    assert(msg_queue_flush_one(&queue, collect, &collector) == 1);
    assert(collector.count == 2);
    assert(collector.values[0] == 2 && collector.values[1] == 3);
    msg_queue_destroy(&queue);
}

static void test_drop_new_and_failed_delivery(void) {
    msg_queue_t queue;
    collector_t collector = {.fail = 1};
    assert(msg_queue_init(&queue, 1, MSG_QUEUE_DROP_NEW) == 0);

    uint8_t first = 4, second = 5;
    gw_msg_t one = message(&first, "one");
    gw_msg_t two = message(&second, "two");
    assert(msg_queue_push(&queue, &one) == 0);
    assert(msg_queue_push(&queue, &two) == 1);
    assert(msg_queue_dropped(&queue) == 1);
    assert(msg_queue_flush_one(&queue, collect, &collector) == -1);
    assert(msg_queue_size(&queue) == 1);

    collector.fail = 0;
    assert(msg_queue_flush_one(&queue, collect, &collector) == 1);
    assert(collector.values[0] == 4);
    msg_queue_destroy(&queue);
}

int main(void) {
    test_drop_oldest_and_deep_copy();
    test_drop_new_and_failed_delivery();
    puts("message queue tests passed");
    return 0;
}

