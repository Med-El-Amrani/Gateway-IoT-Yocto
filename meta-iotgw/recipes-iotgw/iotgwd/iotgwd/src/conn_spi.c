#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>        // close()
#include <sys/ioctl.h>     // ioctl()
#include <linux/spi/spidev.h>

#include "connector_registry.h"
#include "conn_spi.h"
#include "bridge.h"
#include "log.h"
#include <time.h>

/*
la chaîne complète

1. User-space : (on est ici)
ioctl(fd, SPI_IOC_MESSAGE(2), tr); 

2. spidev.c :
spidev_ioctl() → spidev_message()

3. SPI core (drivers/spi/spi.c) :
spi_sync() → appelle master->transfer_one()

4. Driver du contrôleur SPI (ex: spi-bcm2835.c sur Raspberry Pi) :
Écrit dans les registres matériels du contrôleur SPI, déclenche l’horloge, gère MOSI/MISO/CS.
*/
// ---- SPI trace helpers (unconditional to stderr) ----
static inline void dump_hex_stderr(const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) fprintf(stderr, " %02X", b[i]);
    fputc('\n', stderr);
}

static inline const char* op_str(int op){
    switch (op) {
        case SPI_OP_WRITE:    return "WRITE";
        case SPI_OP_READ:     return "READ";
        case SPI_OP_TRANSFER: return "TRANSFER";
        default:              return "?";
    }
}

#define SPI_T(fmt, ...) do { \
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); \
    fprintf(stderr, "[spi %.3f] " fmt "\n", ts.tv_sec + ts.tv_nsec/1e9, ##__VA_ARGS__); \
} while(0)
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

//MODIFIED__
// callback function
void on_spi_rx(const uint8_t* rx, size_t rx_len, void* user, const spi_transaction_t* t)
{
    SPI_T("on_spi_rx invoked, rx_len=%zu (t=%p)", rx_len, (void*)t);
    if (rx && rx_len) { fprintf(stderr, "[spi] RX(cb):"); dump_hex_stderr(rx, rx_len); }
    
    gw_bridge_runtime_t* rt = (gw_bridge_runtime_t*)user;
    if (!rt || !rx || rx_len == 0) return;

    // 1) Dupliquer le buffer RX (le driver le libère après le callback)
    uint8_t* dup = (uint8_t*)malloc(rx_len);
    if (!dup) return;
    memcpy(dup, rx, rx_len);

    // 2) Construire le message "in" conforme à TES structures
    // Zero-init correct pour une struct C avec union :
    gw_msg_t in;
    memset(&in, 0, sizeof(in));

    in.protocole = KIND_SPI;                 // source = SPI
    in.pl.data = dup;                        // payload binaire
    in.pl.len  = rx_len;
    in.pl.is_text = 0;                       // binaire
    in.pl.content_type = "application/octet-stream";  // hint utile pour le transform

    // 3) Appliquer transform (si présent), sinon passer brut
    if (rt->transform) {
        gw_msg_t out;
        memset(&out, 0, sizeof(out));
        int trc = rt->transform(&in, &out, rt->transform_user);
        if (trc == 0) {
            // NOTE: si ta transform ne copie pas le payload, elle peut réutiliser in.pl.*
            // à toi de décider la convention. Ici on envoie 'out' s’il est rempli, sinon 'in'.
            rt->send_fn(rt->send_ctx, &out);
        } else {
            // fallback: brut
            rt->send_fn(rt->send_ctx, &in);
        }
    } else {
        // Pas de transform -> envoi brut
        rt->send_fn(rt->send_ctx, &in);
    }

    log_err("dehors on_spi_rx");

    // 4) Libère la copie locale (si send_fn/transform ne la garde pas)
    // Si ton send_fn/transform ne copie PAS, remplace par une file/buffer persistant.
    free(dup);
}


// Ouvre/configure le périphérique selon cfg. Copie cfg dans le runtime.
// Retour 0 si OK, -1 sinon.
int spi_open_from_config(const spi_connector_t* cfg, spi_runtime_t* rt, spi_msg_cb on_rx, void* user) {
    log_err("Dans spi_open_from_config");
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
    log_err("dehors spi_open_from_config");
    SPI_T("open OK fd=%d dev=%s mode=%u bpw=%u speed=%u lsb=%u txns=%zu",
     rt->fd,
     rt->cfg.device ? rt->cfg.device : "(null)",
     (unsigned)mode, (unsigned)bpw, (unsigned)hz, (unsigned)lsb,
     (size_t)rt->cfg.transactions_count);

    return 0;
}

// Envoie un “paquet” SPI simple :
// - si rx_len == 0 : juste TX (write)
// - si rx_len > 0 et (rx_len != len) : on fait 2 transfers (TX commande, puis RX lecture) avec CS tenu
// - si rx_len > 0 et rx_len == len : un seul transfer full-duplex
// keep_cs (cs_change) ne s’applique qu’entre segments.
static int spi_send_once(spi_runtime_t* rt,
                         const uint8_t* tx, size_t len,
                         uint8_t* rx, size_t rx_len,
                         bool keep_cs, uint32_t speed_hz, uint8_t bpw)
{
    if (!rt || rt->fd < 0) return -1;

    int ret = -1;

    if (rx_len == 0) {
        // TX seulement
        struct spi_ioc_transfer t = {
            .tx_buf = PTR_TO_U64(tx),
            .rx_buf = 0,
            .len = (uint32_t)len,
            .speed_hz = speed_hz,
            .bits_per_word = bpw,
            .cs_change = (uint8_t)keep_cs
        };
        ret = ioctl(rt->fd, SPI_IOC_MESSAGE(1), &t);
        return (ret < 0) ? -1 : 0;
    }

    if (rx_len == len) {
        // Full-duplex 1 segment
        struct spi_ioc_transfer t = {
            .tx_buf = PTR_TO_U64(tx),
            .rx_buf = PTR_TO_U64(rx),
            .len = (uint32_t)len,
            .speed_hz = speed_hz,
            .bits_per_word = bpw,
            .cs_change = 0
        };
        ret = ioctl(rt->fd, SPI_IOC_MESSAGE(1), &t);
        return (ret < 0) ? -1 : 0;
    }

    // Lecture taille différente : 2 segments (TX commande, puis RX lecture avec TX=0x00)
    uint8_t* dummy = NULL;
    if (rx_len > 0) {
        dummy = (uint8_t*)calloc(rx_len, 1); // TX dummy = 0x00 pour clocker la lecture
        if (!dummy) return -1;
    }

    struct spi_ioc_transfer tr[2];
    memset(tr, 0, sizeof(tr));

    tr[0].tx_buf = PTR_TO_U64(tx);
    tr[0].rx_buf = 0;
    tr[0].len = (uint32_t)len;
    tr[0].speed_hz = speed_hz;
    tr[0].bits_per_word = bpw;
    tr[0].cs_change = 1; // garder CS actif entre les deux segments

    tr[1].tx_buf = PTR_TO_U64(dummy);
    tr[1].rx_buf = PTR_TO_U64(rx);
    tr[1].len = (uint32_t)rx_len;
    tr[1].speed_hz = speed_hz;
    tr[1].bits_per_word = bpw;
    tr[1].cs_change = (uint8_t)keep_cs; // si true, CS reste actif après (rarement utile)

    ret = ioctl(rt->fd, SPI_IOC_MESSAGE(2), tr);
    free(dummy);
    return (ret < 0) ? -1 : 0;
}

// Exécute une transaction selon spi_transaction_t et invoque le callback si des RX existent.
int spi_exec_transaction(spi_runtime_t* rt, const spi_transaction_t* t) {
    log_err("Dans spi_exec_transaction");

    if (!rt || !t) return -1;

    // Paramètres effectifs (hérités du cfg)
    uint32_t speed = (uint32_t)(rt->cfg.speed_set ? rt->cfg.speed_hz : 1000000);
    uint8_t  bpw   = (uint8_t) (rt->cfg.bpw_set   ? rt->cfg.bits_per_word : 8);
    bool keep_cs   = (rt->cfg.cs_change_set && rt->cfg.cs_change);

    // bornes
    if (t->len == 0 || t->len > 4096) return -1;
    if (t->has_rx_len && (t->rx_len == 0 || t->rx_len > 4096)) return -1;

    // Préparation du buffer TX
    uint8_t* tx_buf = NULL;
    size_t tx_len = t->len;

    if (t->op == SPI_OP_READ) {
        // read pur : on ne “transmet” rien d’utile → on enverra des 0x00
        tx_buf = (uint8_t*)calloc(tx_len, 1);
        if (!tx_buf) return -1;
    } else {
        // WRITE ou TRANSFER : s’il y a un champ tx, on le parse en hex ; sinon remplissage 0x00
        tx_buf = (uint8_t*)calloc(tx_len, 1);
        if (!tx_buf) return -1;
        if (t->has_tx && t->tx) {
            size_t parsed = 0;
            if (parse_hex_bytes(t->tx, tx_buf, tx_len, &parsed) != 0) {
                // si parsing échoue, on tente copie binaire tronquée (pragmatique)
                size_t src_len = strnlen(t->tx, tx_len);
                memcpy(tx_buf, t->tx, src_len);
            } else if (parsed < tx_len) {
                // si la chaîne ne fournit pas assez d’octets, le reste reste à 0x00
            }
        }
    }

    // Calcul de la taille RX attendue
    size_t rx_len = 0;
    if (t->op == SPI_OP_READ) {
        rx_len = t->has_rx_len ? t->rx_len : t->len; // par défaut, lire len octets
    } else if (t->op == SPI_OP_TRANSFER) {
        rx_len = t->has_rx_len ? t->rx_len : t->len; // idem
    } else { // WRITE
        rx_len = 0;
    }

    //Préparation du buffer RX (si besoin)
    uint8_t* rx_buf = NULL;
    if (rx_len > 0) {
        rx_buf = (uint8_t*)malloc(rx_len);
        if (!rx_buf) { free(tx_buf); return -1; }
        memset(rx_buf, 0, rx_len);
    }

    SPI_T("start op=%s len=%zu rx_len=%zu keep_cs=%d speed=%u bpw=%u", 
    op_str(t->op), tx_len, rx_len, (int)keep_cs, (unsigned)speed, (unsigned)bpw);
    if (tx_buf && tx_len) { fprintf(stderr, "[spi] TX(pre):"); dump_hex_stderr(tx_buf, tx_len); }
    // Exécution selon le type d’opération

    int rc = 0;
    switch (t->op) {
        case SPI_OP_WRITE:
            
            rc = spi_send_once(rt, tx_buf, tx_len, NULL, 0, keep_cs, speed, bpw);
            break;
        case SPI_OP_READ:
            // ici len = longueur de phase "commande" (souvent 1..x) → on enverra des 0x00
            rc = spi_send_once(rt, tx_buf, tx_len, rx_buf, rx_len, keep_cs, speed, bpw);
            SPI_T("done op=%s rc=%d tx_buf=%p rx_buf=%p tx_len=%zu rx_len=%zu",
            op_str(t->op), rc, (void*)tx_buf, (void*)rx_buf, tx_len, rx_len);
            if (tx_buf && tx_len) { fprintf(stderr, "[spi] TX:"); dump_hex_stderr(tx_buf, tx_len); }
            if (rx_buf && rx_len) { fprintf(stderr, "[spi] RX:"); dump_hex_stderr(rx_buf, rx_len); }
            break;
        case SPI_OP_TRANSFER:
            rc = spi_send_once(rt, tx_buf, tx_len, rx_buf, rx_len, keep_cs, speed, bpw);
            SPI_T("done op=%s rc=%d tx_buf=%p rx_buf=%p tx_len=%zu rx_len=%zu",
                op_str(t->op), rc, (void*)tx_buf, (void*)rx_buf, tx_len, rx_len);
            if (tx_buf && tx_len) { fprintf(stderr, "[spi] TX:"); dump_hex_stderr(tx_buf, tx_len); }
            if (rx_buf && rx_len) { fprintf(stderr, "[spi] RX:"); dump_hex_stderr(rx_buf, rx_len); }
            break;
        default:
            rc = -1;
            break;
    }
    // Callback utilisateur si on a reçu des données
    if (rc == 0 && rx_len > 0 && rt->on_rx) {
        rt->on_rx(rx_buf, rx_len, rt->user, t);
    }

    // Nettoyage et code de retour
    free(tx_buf);
    free(rx_buf);

    log_err("Dehors spi_exec_transaction");

    return rc;
}

//MODIFIED__
// Exécute la liste de transactions fournie dans cfg->params.transactions
int spi_run_transactions(spi_runtime_t* rt) {
    SPI_T("run_transactions: count=%zu", (size_t)rt->cfg.transactions_count);
    if (!rt) return -1;
    if (!rt->cfg.transactions || rt->cfg.transactions_count == 0) {
        SPI_T("no transactions configured");
        return 0;
    }

    for (size_t i = 0; i < rt->cfg.transactions_count; ++i) {
        int rc = spi_exec_transaction(rt, &rt->cfg.transactions[i]);
        if (rc != 0) {
            log_warn("spi transaction %zu failed\n", i);
            //return -1;
        }else{
            log_info("SPI transaction %zu succeeded", i);
        }
    }
    SPI_T("run_transactions done (count=%zu)", (size_t)rt->cfg.transactions_count);



    return 0;
}

// Envoi ad-hoc (hors liste), pratique pour un “adapter” style mqtt_send_adapter
// Si rx_len > 0, on renvoie via callback (si défini).
int spi_send_adapter(const uint8_t* tx, size_t len, size_t rx_len, void* ctx) {
    spi_runtime_t* rt = (spi_runtime_t*)ctx;
    if (!rt || rt->fd < 0 || !tx || len == 0) return -1;

    uint32_t speed = (uint32_t)(rt->cfg.speed_set ? rt->cfg.speed_hz : 1000000);
    uint8_t  bpw   = (uint8_t) (rt->cfg.bpw_set   ? rt->cfg.bits_per_word : 8);
    bool keep_cs   = (rt->cfg.cs_change_set && rt->cfg.cs_change);

    uint8_t* tx_buf = (uint8_t*)malloc(len);
    if (!tx_buf) return -1;
    memcpy(tx_buf, tx, len);

    uint8_t* rx_buf = NULL;
    if (rx_len > 0) {
        rx_buf = (uint8_t*)malloc(rx_len);
        if (!rx_buf) { free(tx_buf); return -1; }
        memset(rx_buf, 0, rx_len);
    }

    int rc = spi_send_once(rt, tx_buf, len, rx_buf, rx_len, keep_cs, speed, bpw);

    if (rc == 0 && rx_buf && rt->on_rx) {
        // Pas de spi_transaction_t formel ici → on passe NULL
        rt->on_rx(rx_buf, rx_len, rt->user, NULL);
    }

    free(tx_buf);
    free(rx_buf);
    return rc;
}


static void* spi_poll_thread(void* arg) {


    spi_runtime_t* rt = (spi_runtime_t*)arg;
    const int sleep_ms = (rt->poll_ms > 0 ? rt->poll_ms : 1000);

    SPI_T("poll thread start (period=%d ms)", sleep_ms);

    while (!rt->stop_flag) {
        // re-run the configured transactions; on_rx() will be invoked for RX
        (void)spi_run_transactions(rt);
        if (sleep_ms > 0) usleep(sleep_ms * 1000);
        else sched_yield();
    }
    SPI_T("poll thread stop");

    return NULL;
}

int spi_start_polling(spi_runtime_t* rt, int poll_ms) {
    if (!rt) return -1;
    if (rt->polling) return 0;
    rt->poll_ms   = (poll_ms > 0 ? poll_ms : 1000);
    rt->stop_flag = 0;
    rt->polling   = 1;
    if (pthread_create(&rt->thread, NULL, spi_poll_thread, rt) != 0) {
        perror("pthread_create(spi_poll_thread)");
        rt->polling = 0;
        return -1;
    }
    return 0;
}

void spi_stop_polling(spi_runtime_t* rt) {
    if (!rt || !rt->polling) return;
    rt->stop_flag = 1;
    pthread_join(rt->thread, NULL);
    rt->polling = 0;
}


void spi_close(spi_runtime_t* rt) {
    if (!rt) return;
    spi_stop_polling(rt);
    if (rt->fd >= 0) close(rt->fd);
    rt->fd = -1;
    memset(rt, 0, sizeof(*rt));
}