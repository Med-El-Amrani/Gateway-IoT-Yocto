#pragma once
#include "connector.h"
#include <stdint.h>
#include <stddef.h>

/**
 * Crée un connecteur SPI.
 *
 * @param id           Identifiant logique (ex: "spi0").
 * @param device       Chemin spidev (ex: "/dev/spidev0.0").
 * @param mode         Mode SPI 0..3 (flags SPI_MODE_* possibles).
 * @param speed_hz     Fréquence (ex: 500000).
 * @param bits_per_word Bits par mot (8 typiquement).
 * @param delay_usecs  Délai entre transferts (us), 0 par défaut.
 * @param rx_len       Taille de réception à lire à chaque poll (1..32).
 * @param tx_template  Buffer TX optionnel (utilisé à chaque poll si non-NULL).
 * @param tx_len       Longueur de tx_template (0..32).
 *
 * Remarque : à chaque `poll()`, on lance un transfert full-duplex
 * (tx_template -> tx, rx_len -> rx), puis on met à jour une valeur
 * flottante `last_value` à partir des premiers octets reçus.
 */
connector_t* conn_spi_create(const char *id,
                             const char *device,
                             uint8_t     mode,
                             uint32_t    speed_hz,
                             uint8_t     bits_per_word,
                             uint16_t    delay_usecs,
                             size_t      rx_len,
                             const uint8_t *tx_template,
                             size_t      tx_len);

void conn_spi_destroy(connector_t *c);
