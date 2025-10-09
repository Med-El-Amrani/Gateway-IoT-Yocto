// demo_spi_poll.c — test du polling SPI (transactions périodiques)
#include "conn_spi.h"
#include <stdio.h>
#include <signal.h>
#include <unistd.h>   // sleep, usleep
#include <time.h>     // clock_gettime

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

static void demo_on_spi_rx(const uint8_t* rx, size_t rx_len,
                           void* user, const spi_transaction_t* t)
{
    (void)user; (void)t;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("[%.3f] [SPI RX] %zu bytes:", ts.tv_sec + ts.tv_nsec/1e9, rx_len);
    for (size_t i = 0; i < rx_len; i++) printf(" %02X", rx[i]);
    printf("\n");
    fflush(stdout);
}

int main(void) {
    signal(SIGINT, on_sigint);

    // ---- Config SPI
    spi_connector_t cfg = (spi_connector_t){0};
    cfg.params.device = "/dev/spidev0.0";   // change en /dev/spidev0.1 si CS1
    cfg.params.mode = 0;            cfg.params.mode_set = true;
    cfg.params.bits_per_word = 8;   cfg.params.bpw_set = true;
    cfg.params.speed_hz = 1000000;  cfg.params.speed_set = true;

    // ---- Déclare 1 transaction dans la config pour le polling
    static spi_transaction_t txs[1];
    txs[0] = (spi_transaction_t){0};
    txs[0].op = SPI_OP_TRANSFER;       // full-duplex
    txs[0].len = 2;                    // on émet 2 octets
    txs[0].has_tx = true;
    txs[0].tx = "0xA55A";              // A5 5A sur MOSI
    txs[0].has_rx_len = true;
    txs[0].rx_len = 2;                 // on lit 2 octets sur MISO

    cfg.params.transactions = txs;
    cfg.params.transactions_count = 1;
    // (optionnel) garder CS entre segments si tu faisais une lecture en 2 phases:
    // cfg.params.cs_change_set = true; cfg.params.cs_change = true;

    // ---- Ouvre et démarre le polling
    spi_runtime_t rt;
    if (spi_open_from_config(&cfg, &rt, demo_on_spi_rx, NULL) != 0) {
        fprintf(stderr, "Failed to open SPI\n");
        return 1;
    }

    // Lancement du thread de polling: toutes les 200 ms
    if (spi_start_polling(&rt, 200) != 0) {
        fprintf(stderr, "spi_start_polling failed\n");
        spi_close(&rt);
        return 1;
    }

    printf("Polling started (200 ms). Press Ctrl+C to stop…\n");

    // Boucle d’attente jusqu’à Ctrl+C
    while (!g_stop) {
        sleep(1);
    }

    // Arrêt propre
    spi_stop_polling(&rt);
    spi_close(&rt);
    printf("Stopped.\n");
    return 0;
}
