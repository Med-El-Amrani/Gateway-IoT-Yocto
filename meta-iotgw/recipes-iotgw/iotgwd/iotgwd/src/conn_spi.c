#include "conn_spi.h"
#include "log.h"

#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

// ---- Contexte interne -------------------------------------------------------

typedef struct {
    int        fd;
    char       devpath[128];

    uint8_t    mode;
    uint8_t    bits;
    uint32_t   speed_hz;
    uint16_t   delay_us;

    uint8_t    txbuf[32];
    size_t     txlen;

    uint8_t    rxbuf[32];
    size_t     rxlen;

    // Valeur "courante" extraite du rxbuf par poll()
    double     last_value;
} spi_ctx_t;

// ---- Helpers ----------------------------------------------------------------

static int spi_setup(spi_ctx_t *ctx) {
    // Mode
    if (ioctl(ctx->fd, SPI_IOC_WR_MODE, &ctx->mode) == -1) {
        log_err("SPI(%s): set mode failed (errno=%d)", ctx->devpath, errno);
        return -1;
    }
    // Bits
    if (ioctl(ctx->fd, SPI_IOC_WR_BITS_PER_WORD, &ctx->bits) == -1) {
        log_err("SPI(%s): set bits failed (errno=%d)", ctx->devpath, errno);
        return -1;
    }
    // Vitesse
    if (ioctl(ctx->fd, SPI_IOC_WR_MAX_SPEED_HZ, &ctx->speed_hz) == -1) {
        log_err("SPI(%s): set speed failed (errno=%d)", ctx->devpath, errno);
        return -1;
    }
    return 0;
}

// Convertit les premiers octets du rx en un double simple à partir d’un u32 BE
static double parse_rx_to_double(const uint8_t *rx, size_t n) {
    if (n >= 4) {
        uint32_t v = (uint32_t)rx[0] << 24 |
                     (uint32_t)rx[1] << 16 |
                     (uint32_t)rx[2] <<  8 |
                     (uint32_t)rx[3] <<  0;
        return (double)v;
    } else if (n >= 2) {
        uint16_t v = (uint16_t)rx[0] << 8 | (uint16_t)rx[1];
        return (double)v;
    } else if (n >= 1) {
        return (double)rx[0];
    }
    return 0.0;
}

// ---- Implémentation des callbacks connector ---------------------------------

static int spi_read(connector_t *c, const char *key, double *out) {
    if (!c || !c->impl || !out) return -EINVAL;
    spi_ctx_t *ctx = (spi_ctx_t*)c->impl;

    // Pour l’instant, on supporte une clé unique "value"
    if (!key || strcmp(key, "value") == 0) {
        *out = ctx->last_value;
        return 0;
    }
    return -ENOENT;
}

static int spi_write(connector_t *c, const char *key, double val) {
    (void)key; (void)val;
    // Non supporté (tu pourras l’étendre pour écrire une consigne vers un périphérique SPI)
    return -ENOTSUP;
}

static void spi_poll(connector_t *c) {
    if (!c || !c->impl) return;
    spi_ctx_t *ctx = (spi_ctx_t*)c->impl;
    if (ctx->rxlen == 0) return;

    struct spi_ioc_transfer xfer = {0};
    xfer.tx_buf        = (unsigned long)(ctx->txlen ? ctx->txbuf : NULL);
    xfer.rx_buf        = (unsigned long)ctx->rxbuf;
    xfer.len           = (uint32_t)ctx->rxlen;
    xfer.speed_hz      = ctx->speed_hz;
    xfer.bits_per_word = ctx->bits;
    xfer.delay_usecs   = ctx->delay_us;

    int r = ioctl(ctx->fd, SPI_IOC_MESSAGE(1), &xfer);
    if (r < 0) {
        log_warn("SPI(%s): transfer failed (errno=%d)", ctx->devpath, errno);
        return;
    }

    // Met à jour la valeur "logique" exposée par read("value")
    ctx->last_value = parse_rx_to_double(ctx->rxbuf, ctx->rxlen);
    // Optionnel : log pour debug (à désactiver en prod)
    // log_info("SPI(%s): value=%f", ctx->devpath, ctx->last_value);
}

// ---- API publique ------------------------------------------------------------

connector_t* conn_spi_create(const char *id,
                             const char *device,
                             uint8_t     mode,
                             uint32_t    speed_hz,
                             uint8_t     bits_per_word,
                             uint16_t    delay_usecs,
                             size_t      rx_len,
                             const uint8_t *tx_template,
                             size_t      tx_len)
{
    if (!id || !device) return NULL;
    if (rx_len > 32 || tx_len > 32) return NULL;

    spi_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    // Ouvre le device
    ctx->fd = open(device, O_RDWR | O_CLOEXEC);
    if (ctx->fd < 0) {
        log_err("SPI(%s): open failed (errno=%d)", device, errno);
        free(ctx);
        return NULL;
    }

    strncpy(ctx->devpath, device, sizeof(ctx->devpath)-1);
    ctx->mode      = mode;
    ctx->bits      = bits_per_word ? bits_per_word : 8;
    ctx->speed_hz  = speed_hz ? speed_hz : 500000;
    ctx->delay_us  = delay_usecs;
    ctx->rxlen     = rx_len;

    if (tx_template && tx_len) {
        memcpy(ctx->txbuf, tx_template, tx_len);
        ctx->txlen = tx_len;
    }

    if (spi_setup(ctx) != 0) {
        close(ctx->fd);
        free(ctx);
        return NULL;
    }

    // Alloue le wrapper connector
    connector_t *c = calloc(1, sizeof(*c));
    if (!c) {
        close(ctx->fd);
        free(ctx);
        return NULL;
    }
    c->id    = strdup(id);
    c->type  = CONN_SPI;
    c->impl  = ctx;
    c->read  = spi_read;
    c->write = spi_write;
    c->poll  = spi_poll;

    log_info("SPI connector '%s' ready (%s, mode=%u, %uHz, %ubit, rx_len=%zu)",
             id, device, (unsigned)mode, (unsigned)speed_hz,
             (unsigned)ctx->bits, ctx->rxlen);

    return c;
}

void conn_spi_destroy(connector_t *c) {
    if (!c) return;
    spi_ctx_t *ctx = (spi_ctx_t*)c->impl;
    if (ctx) {
        if (ctx->fd >= 0) close(ctx->fd);
        free(ctx);
    }
    if (c->id) free((void*)c->id);
    free(c);
}
