#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "websocket_history.h"

static void expect_next(websocket_history_t *history, uint64_t *cursor,
                        const char *expected, uint64_t expected_missed) {
    uint8_t *data = NULL;
    size_t len = 0;
    int is_text = 0;
    uint64_t missed = 0;
    assert(websocket_history_copy_next(history, cursor, &data, &len,
                                       &is_text, &missed) == 0);
    assert(len == strlen(expected));
    assert(memcmp(data, expected, len) == 0);
    assert(is_text == 1);
    assert(missed == expected_missed);
    free(data);
}

int main(void) {
    websocket_history_t history;
    assert(websocket_history_init(&history, 2) == 0);
    uint64_t live_client = websocket_history_next_sequence(&history);
    assert(websocket_history_push(&history, (const uint8_t *)"one", 3, 1) == 0);
    expect_next(&history, &live_client, "one", 0);

    uint64_t slow_client = live_client;
    assert(websocket_history_push(&history, (const uint8_t *)"two", 3, 1) == 0);
    assert(websocket_history_push(&history, (const uint8_t *)"three", 5, 1) == 0);
    assert(websocket_history_push(&history, (const uint8_t *)"four", 4, 1) == 0);
    expect_next(&history, &slow_client, "three", 1);
    expect_next(&history, &slow_client, "four", 0);
    assert(websocket_history_copy_next(&history, &slow_client, NULL, NULL,
                                       NULL, NULL) == -1);
    websocket_history_destroy(&history);
    return 0;
}
