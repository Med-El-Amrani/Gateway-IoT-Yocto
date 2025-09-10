#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdatomic.h>

static atomic_int running = 1;
static void on_sigint(int sig) { (void)sig; running = 0; }

int main(void) {
    signal(SIGINT, on_sigint);
    fprintf(stderr, "[iotgwd] starting...\n");
    while (running) {
        fprintf(stderr, "[iotgwd] alive\n");
        sleep(5);
    }
    fprintf(stderr, "[iotgwd] stopping.\n");
    return 0;
}
