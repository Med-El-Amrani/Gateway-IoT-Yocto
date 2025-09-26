// ================================
// File: conn_uart.c
// ================================
#define _GNU_SOURCE
#include "conn_uart.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>




// Some platforms may not define higher baud constants; guard with #ifdef.
static int set_speed(int fd, int baudrate) {
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) return UART_ERR_TCGETS;

    speed_t spd;
    switch (baudrate) {
        case 1200: spd = B1200; break;
        case 2400: spd = B2400; break;
        case 4800: spd = B4800; break;
        case 9600: spd = B9600; break;
        case 19200: spd = B19200; break;
        case 38400: spd = B38400; break;
#ifdef B57600
        case 57600: spd = B57600; break;
#endif
#ifdef B115200
        case 115200: spd = B115200; break;
#endif
#ifdef B230400
        case 230400: spd = B230400; break;
#endif
#ifdef B460800
        case 460800: spd = B460800; break;
#endif
#ifdef B921600
        case 921600: spd = B921600; break;
#endif
        default:
            return UART_ERR_BAUD;
    }

    if (cfsetispeed(&tio, spd) < 0) return UART_ERR_BAUD;
    if (cfsetospeed(&tio, spd) < 0) return UART_ERR_BAUD;
    if (tcsetattr(fd, TCSANOW, &tio) < 0) return UART_ERR_TCSETS;
    return UART_OK;
}

static int set_bytesize_parity_stop(int fd, const uart_params_t *p) {
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) return UART_ERR_TCGETS;

    // Raw mode baseline
    cfmakeraw(&tio);

    // Byte size
    tio.c_cflag &= ~CSIZE;
    switch (p->bytesize_set ? p->bytesize : 8) {
        case 5: tio.c_cflag |= CS5; break;
        case 6: tio.c_cflag |= CS6; break;
        case 7: tio.c_cflag |= CS7; break;
        case 8: tio.c_cflag |= CS8; break;
        default: return UART_ERR_BYTESIZE;
    }

    // Parity
    char parity = p->parity_set ? p->parity : 'N';
    tio.c_cflag &= ~(PARENB | PARODD);
#ifdef CMSPAR
    tio.c_cflag &= ~CMSPAR;
#endif

    switch (parity) {
        case 'N':
            // no parity
            break;
        case 'E':
            tio.c_cflag |= PARENB; // even when PARODD cleared
            // PARODD=0 => even
            break;
        case 'O':
            tio.c_cflag |= (PARENB | PARODD); // odd
            break;
        case 'M': // Mark parity (parity bit = 1), requires CMSPAR
#ifdef CMSPAR
            tio.c_cflag |= (PARENB | CMSPAR | PARODD); // mark
            break;
#else
            return UART_ERR_PARITY;
#endif
        case 'S': // Space parity (parity bit = 0), requires CMSPAR
#ifdef CMSPAR
            tio.c_cflag |= (PARENB | CMSPAR); // space (PARODD=0)
            break;
#else
            return UART_ERR_PARITY;
#endif
        default:
            return UART_ERR_PARITY;
    }

    // Stop bits
    double sb = p->stopbits_set ? p->stopbits : 1.0;
    if (sb == 1.0) {
        tio.c_cflag &= ~CSTOPB;
    } else if (sb == 2.0) {
        tio.c_cflag |= CSTOPB;
    } else if (sb == 1.5) {
        // POSIX termios generally does not support 1.5 stop bits. Return unsupported.
        return UART_ERR_STOPBITS;
    } else {
        return UART_ERR_STOPBITS;
    }

    // Flow control
    // Hardware (RTS/CTS)
#ifdef CRTSCTS
    if (p->rtscts_set ? p->rtscts : false) tio.c_cflag |= CRTSCTS; else tio.c_cflag &= ~CRTSCTS;
#else
    if (p->rtscts_set && p->rtscts) return UART_ERR_UNSUPPORTED;
#endif

    // Software (XON/XOFF)
    if (p->xonxoff_set ? p->xonxoff : false) {
        tio.c_iflag |= (IXON | IXOFF);
    } else {
        tio.c_iflag &= ~(IXON | IXOFF);
    }

    // Read timeout
    int tmo_ms = p->timeout_set ? p->timeout_ms : 1000;
    if (tmo_ms < 0 || tmo_ms > 60000) return UART_ERR_TIMEOUT;
    // VTIME is in 100ms ticks. VMIN=0 => return immediately with available bytes, or after timeout.
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = (cc_t)((tmo_ms + 99) / 100); // round up

    // Local flags
    tio.c_cflag |= (CLOCAL | CREAD);

    if (tcsetattr(fd, TCSANOW, &tio) < 0) return UART_ERR_TCSETS;
    return UART_OK;
}

int uart_apply_settings(int fd, const uart_params_t *params) {
    if (!params) return UART_ERR_ARG;
    int rc = set_speed(fd, params->baudrate);
    if (rc != UART_OK) return rc;
    rc = set_bytesize_parity_stop(fd, params);
    if (rc != UART_OK) return rc;
    return UART_OK;
}

int uart_open(const uart_params_t *params, int *out_fd) {
    if (!params || !params->port || !out_fd) return UART_ERR_ARG;

    int fd = open(params->port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return UART_ERR_OPEN;

    // Clear O_NONBLOCK after open so termios timeouts (VTIME) work as expected
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    int rc = uart_apply_settings(fd, params);
    if (rc != UART_OK) {
        close(fd);
        return rc;
    }

    *out_fd = fd;
    return UART_OK;
}

int uart_flush(int fd, bool both) {
    return tcflush(fd, both ? TCIOFLUSH : TCIFLUSH);
}

static int wait_writable(int fd, int timeout_ms) {
    if (timeout_ms < 0) return 1; // block in write()
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv = { .tv_sec = timeout_ms/1000, .tv_usec = (timeout_ms%1000)*1000 };
    int r = select(fd+1, NULL, &wfds, NULL, &tv);
    return (r > 0) ? 1 : r; // 1 if ready, 0 on timeout, -1 on error
}

ssize_t uart_write(int fd, const uint8_t *buf, size_t len, int write_timeout_ms) {
    size_t off = 0;
    while (off < len) {
        int rdy = wait_writable(fd, write_timeout_ms);
        if (rdy <= 0) {
            if (rdy == 0) errno = EAGAIN; // timeout
            return -1;
        }
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return (ssize_t)off;
}

ssize_t uart_read(int fd, uint8_t *buf, size_t len) {
    for (;;) {
        ssize_t n = read(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
        }
        return n; // may be 0 on timeout
    }
}

// --- Hex parsing -----------------------------------------------------------
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

int uart_parse_hex(const char *hex_str, uint8_t *out, size_t max_out) {
    if (!hex_str || !out) return UART_ERR_ARG;
    size_t i = 0;
    // Skip optional 0x/0X
    if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) hex_str += 2;

    size_t len = strlen(hex_str);
    if (len == 0 || (len % 2) != 0) return UART_ERR_ARG; // need even number of nibbles

    size_t out_len = len / 2;
    if (out_len > max_out) return UART_ERR_ARG;

    for (i = 0; i < out_len; ++i) {
        int hi = hexval(hex_str[2*i]);
        int lo = hexval(hex_str[2*i + 1]);
        if (hi < 0 || lo < 0) return UART_ERR_ARG;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)out_len;
}

// --- Packet read -----------------------------------------------------------
static inline bool match_tail(const uint8_t *buf, size_t buf_len,
                            const uint8_t *pat, size_t pat_len) {
    if (pat_len == 0 || buf_len < pat_len) return false;
    return memcmp(buf + (buf_len - pat_len), pat, pat_len) == 0;
}

static int load_delim(const char *hex, uint8_t *buf, size_t *len_io) {
    if (!hex) { *len_io = 0; return UART_OK; }
    int n = uart_parse_hex(hex, buf, *len_io);
    if (n < 0) return n;
    *len_io = (size_t)n;
    return UART_OK;
}

int uart_read_packet(int fd, const uart_params_t *params,
                     uint8_t *out_buf, size_t out_buf_sz, size_t *out_len) {
    if (!params || !params->has_packet || !out_buf || !out_len) return UART_ERR_ARG;

    uint8_t start[32], end[32];
    size_t start_len = sizeof(start), end_len = sizeof(end);

    int rc = load_delim(params->packet.start, start, &start_len);
    if (rc < 0) return UART_ERR_PACKET_CFG;
    rc = load_delim(params->packet.end, end, &end_len);
    if (rc < 0) return UART_ERR_PACKET_CFG;

    bool use_start = start_len > 0;
    bool use_end   = end_len > 0;
    bool use_len   = params->packet.length_set && params->packet.length > 0;

    if (!(use_end || use_len)) {
        // Must have at least an end delimiter or a fixed length
        return UART_ERR_PACKET_CFG;
    }

    size_t pos = 0;
    *out_len = 0;

    

    // 1) Seek start (if any)
    if (use_start) {
        uint8_t b;
        for (;;) {
            ssize_t n = uart_read(fd, &b, 1);
            if (n <= 0) return (int)n; // 0 on timeout, <0 on error
            // shift window
            if (pos < start_len) {
                out_buf[pos++] = b;
            } else {
                memmove(out_buf, out_buf + 1, start_len - 1);
                out_buf[start_len - 1] = b;
            }
            if (match_tail(out_buf, pos, start, start_len)) {
                pos = 0; // reset buffer for payload
                break;
            }
        }
    }

    // 2) Read payload depending on length/end
    if (use_len && !use_end) {
        // Read exactly length bytes
        size_t need = (size_t)params->packet.length;
        if (need > out_buf_sz) return UART_ERR_ARG;
        while (pos < need) {
            ssize_t n = uart_read(fd, out_buf + pos, need - pos);
            if (n <= 0) return (int)n;
            pos += (size_t)n;
        }
        *out_len = pos;
        return UART_OK;
    }

    // With end delimiter (with/without length cap)
    size_t cap = out_buf_sz;
    if (use_len && params->packet.length < (int)cap) cap = (size_t)params->packet.length;
    if (cap == 0) return UART_ERR_ARG;

    for (;;) {
        uint8_t b;
        ssize_t n = uart_read(fd, &b, 1);
        if (n <= 0) return (int)n;
        if (pos >= cap) return UART_ERR_ARG; // overflow risk
        out_buf[pos++] = b;
        if (match_tail(out_buf, pos, end, end_len)) {
            *out_len = pos - end_len; // exclude end delimiter
            return UART_OK;
        }
    }
}

int uart_close(int fd) {
    return close(fd);
}
