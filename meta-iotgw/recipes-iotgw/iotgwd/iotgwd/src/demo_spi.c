#include "conn_spi.h"
#include <stdio.h>

static void demo_on_spi_rx(const uint8_t* rx, size_t rx_len,
                      void* user, const spi_transaction_t* t)
{
    (void)user; (void)t;
    printf("[SPI RX] %zu bytes:", rx_len);
    for (size_t i=0; i<rx_len; i++) printf(" %02X", rx[i]);
    printf("\n");
}

int main(void) {
    spi_connector_t cfg = {0};
    cfg.params.device = "/dev/spidev0.0";
    cfg.params.mode = 0;         cfg.params.mode_set = true;
    cfg.params.bits_per_word = 8; cfg.params.bpw_set = true;
    cfg.params.speed_hz = 1000000; cfg.params.speed_set = true;

    spi_runtime_t rt;
    if (spi_open_from_config(&cfg, &rt, demo_on_spi_rx, NULL) != 0) {
        fprintf(stderr, "Failed to open SPI\n");
        return 1;
    }

    spi_transaction_t t = {0};
    t.op = SPI_OP_TRANSFER;
    t.len = 2;
    t.tx = "0xA55A";   // commande JEDEC ID
    t.has_tx = true;
    t.rx_len = 2;    // lire 3 octets en réponse
    t.has_rx_len = true;

    spi_exec_transaction(&rt, &t);

    spi_close(&rt);
    return 0;
}
