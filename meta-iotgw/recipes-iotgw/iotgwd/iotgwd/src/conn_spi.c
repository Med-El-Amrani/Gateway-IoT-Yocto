#include <stdio.h>
#include <stdint.h>
#include <linux/ioctl.h>
#include <linux/spi/spidev.h>

#include "connector_registry.h"
#include "conn_spi.h"


//------------ Helpers --------------

//renvoie true si c'est un hex digit
static inline bool is_hex(char c){
    return  (c>='0' && c<='9')  ||
            (c>='a' && c<='f')  ||
            (c>= 'A' && c<='F');
}

//convertit un nibble ASCII en valeur 0...15 (précondition: is_hex)
static inline uint8_t hex_val(char c){
    if(c>='0' && c<='9') return (uint8_t)(c-'0');
    if(c >= 'a' && c<='f') return (uint8_t)(10+(c-'a'));
    return (uint8_t)(10+(c-'A'));
}

// convertit hex en array de uint8_t 
static int parse_hex_bytes(const char* s,
                           uint8_t* out,
                           size_t max_out,
                           size_t* out_len)
{
    if (!s || !out || max_out == 0) return -1;

    // Skip "0x" / "0X" éventuel
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;

    int have_high = 0;
    uint8_t high = 0;
    size_t n = 0;

    for (const char* p = s; *p; ++p) {
        if (!is_hex(*p)) continue;
        uint8_t v = hex_val(*p);

        if (!have_high) {
            high = v;
            have_high = 1;
        } else {
            if (n >= max_out) break;
            out[n++] = (uint8_t)((high << 4) | v);
            have_high = 0;
        }
    }

    // gérer le cas impair → dernier nibble devient un octet 0x0X
    if (have_high && n < max_out) {
        out[n++] = (uint8_t)(high & 0x0F);
    }

    if (n == 0) return -1;
    *out_len = n;
    return 0;
}


// Définit un champ via ioctl WR/RD + vérif lecture si possible.
// Macro utilitaire pour écraser du code boilerplate.
#define SPI_TRY_SET(fd, WR_REQ, RD_REQ, val_ptr)                    \
    do {                                                            \
        if (ioctl((fd), (WR_REQ), (val_ptr)) < 0) {                 \
            perror(#WR_REQ);                                        \
            return -1;                                              \
        }                                                           \
        if ((RD_REQ) != 0) {                                        \
            typeof(*(val_ptr)) __tmp = 0;                           \
            if (ioctl((fd), (RD_REQ), &__tmp) < 0) {                \
                perror(#RD_REQ);                                    \
                return -1;                                          \
            }                                                       \
        }                                                           \
    } while (0)

// Cast portable pour tx_buf/rx_buf (__u64 côté kernel)
#define PTR_TO_U64(p) ((uintptr_t)(p))

// ------------------ API ------------------

// Ouvre/configure le périphérique selon cfg. Copie cfg dans le runtime.
// Retour 0 si OK, -1 sinon.
int spi_open_from_config(const spi_connector_t* cfg, spi_runtime_t* rt, spi_msg_cb on_rx, void* user) {
    if (!cfg || !rt || !cfg->params.device) return -1;
    memset(rt, 0, sizeof(*rt));

    rt->fd = open(cfg->params.device, O_RDWR);
    if (rt->fd < 0) {
        perror("open(spi)");
        return -1;
    }

    // Snapshot params pour réutilisation
    rt->cfg = cfg->params;
    rt->on_rx = on_rx;
    rt->user = user;

    // Valeurs par défaut si non-set
    uint8_t mode = (uint8_t)(rt->cfg.mode_set ? rt->cfg.mode : 0); // 0..3
    uint8_t bpw  = (uint8_t)(rt->cfg.bpw_set ? rt->cfg.bits_per_word : 8); // 8/16/32
    uint32_t hz  = (uint32_t)(rt->cfg.speed_set ? rt->cfg.speed_hz : 1000000); // 1 MHz
    uint8_t lsb  = (uint8_t)(rt->cfg.lsb_first_set && rt->cfg.lsb_first ? 1 : 0);

    // Appliquer mode (legacy 8-bit suffit pour 0..3)
    SPI_TRY_SET(rt->fd, SPI_IOC_WR_MODE, SPI_IOC_RD_MODE, &mode);

    // Bits/word
    SPI_TRY_SET(rt->fd, SPI_IOC_WR_BITS_PER_WORD, SPI_IOC_RD_BITS_PER_WORD, &bpw);

    // Speed
    SPI_TRY_SET(rt->fd, SPI_IOC_WR_MAX_SPEED_HZ, SPI_IOC_RD_MAX_SPEED_HZ, &hz);

#ifdef SPI_IOC_WR_LSB_FIRST
    // LSB-first (optionnel suivant kernel)
    if (lsb) {
        SPI_TRY_SET(rt->fd, SPI_IOC_WR_LSB_FIRST, SPI_IOC_RD_LSB_FIRST, &lsb);
    } else {
        uint8_t zero = 0;
        SPI_TRY_SET(rt->fd, SPI_IOC_WR_LSB_FIRST, SPI_IOC_RD_LSB_FIRST, &zero);
    }
#else
    (void)lsb; // si pas supporté par l’en-tête
#endif

    return 0;
}

