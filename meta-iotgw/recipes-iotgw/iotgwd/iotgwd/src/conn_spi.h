#ifndef CONN_SPI_H
#define CONN_SPI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // size_t
//------- Runtime SPI--------------
typedef struct {
    int fd;
    spi_params_t cfg;
    spi_msg_cb on_rx;
    void* user;
} spi_runtime_t;

typedef void (*spi_msg_cb)(const uint8_t* rx,size_t rx_len, void* user, const spi_transaction_t* t);
// -------- API publique --------

// Ouvre le périphérique SPI avec les paramètres du connecteur
int spi_open_from_config(const spi_connector_t* cfg,
                         spi_runtime_t* rt,
                         spi_msg_cb on_rx,
                         void* user);

// Exécute toutes les transactions listées dans cfg->params.transactions
int spi_run_transactions(spi_runtime_t* rt);

// Exécute une transaction unique (hors liste)
int spi_exec_transaction(spi_runtime_t* rt,
                         const spi_transaction_t* t);

// Envoi ad-hoc (données TX/len, optionnel RX)
int spi_send_adapter(const uint8_t* tx,
                     size_t len,
                     size_t rx_len,
                     void* ctx);

// Ferme le périphérique et nettoie le runtime
void spi_close(spi_runtime_t* rt);

#endif // CONN_SPI_H