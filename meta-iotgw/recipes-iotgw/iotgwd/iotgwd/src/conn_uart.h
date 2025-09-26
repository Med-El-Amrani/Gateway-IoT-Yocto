// ================================
// File: conn_uart.h
// ================================
#ifndef CONN_UART_H
#define CONN_UART_H


#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>


// The following structs are defined in connectors.h (as provided by you):
// - uart_packet_t
// - uart_params_t
// - uart_connector_t
// Include your project's header path as needed.
#include "connectors.h"


#ifdef __cplusplus
extern "C" {
#endif
// ================================
// Return codes (negative values indicate errors)
#define UART_OK                 (0)
#define UART_ERR_OPEN          (-1)
#define UART_ERR_TCGETS        (-2)
#define UART_ERR_TCSETS        (-3)
#define UART_ERR_BAUD          (-4)
#define UART_ERR_BYTESIZE      (-5)
#define UART_ERR_PARITY        (-6)
#define UART_ERR_STOPBITS      (-7)
#define UART_ERR_TIMEOUT       (-8)
#define UART_ERR_ARG           (-9)
#define UART_ERR_PACKET_CFG    (-10)
#define UART_ERR_UNSUPPORTED   (-11)



// Opens the port described by params->port and applies all termios settings.
// On success returns UART_OK and sets *out_fd to a valid file descriptor.
int uart_open(const uart_params_t *params, int *out_fd);


// Applies termios based on params to an already-open fd.
int uart_apply_settings(int fd, const uart_params_t *params);


// Flush input/output buffers ("both" if both=true, otherwise input only)
int uart_flush(int fd, bool both);


// Write data; if write_timeout_ms >= 0, a best-effort blocking write with
// per-call timeout using select(). Returns bytes written or <0 on error.
ssize_t uart_write(int fd, const uint8_t *buf, size_t len, int write_timeout_ms);


// Read up to len bytes honoring the termios VTIME/VMIN configured by params.
// Returns bytes read, 0 on timeout, or <0 on error.
ssize_t uart_read(int fd, uint8_t *buf, size_t len);


// Read a framed packet based on params->packet.
// - If start is set, consume until start sequence is matched (start not included
// in payload).
// - If length_set, read exactly `length` bytes of payload (after optional start).
// - If end is set, keep reading until end sequence is matched; the end delimiter
// is not included in the returned payload.
// You may combine start+length, start+end, or length-only, end-only. At least
// one of {length_set, end} must be true when has_packet is true.
// Returns UART_OK on success and sets *out_len. Returns 0-length payload on
// clean timeout. Negative error code on failure.
int uart_read_packet(int fd, const uart_params_t *params,
uint8_t *out_buf, size_t out_buf_sz, size_t *out_len);


// Close fd. Returns 0 on success, -1 on error (errno set).
int uart_close(int fd);


// --- Utilities -------------------------------------------------------------
// Parse a hex string like "0x7E" or "AA55" (even number of hex chars) into
// bytes. Returns number of bytes written, or <0 on error.
int uart_parse_hex(const char *hex_str, uint8_t *out, size_t max_out);


#ifdef __cplusplus
}
#endif


#endif // CONN_UART_H