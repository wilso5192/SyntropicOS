/**
 * @file syn_port_serial.h
 * @brief Console serial port interface.
 *
 * Provides the low-level byte I/O primitives for the system console.
 * The console is a singleton — there is exactly one per project. It may
 * be backed by a hardware UART, USB CDC, or any other byte stream.
 *
 * The CLI and logging system use this interface directly — no callback
 * wiring is needed in application code.
 *
 * @par Contract
 * - `write` may briefly block for buffer space but must not spin indefinitely.
 * - `read` is strictly **non-blocking**: returns 0 if nothing is available.
 *
 * @par Peripheral UARTs
 * For non-console UARTs (Modbus, GPS, sensors), use syn_port_uart.h
 * which provides instance-based access to hardware UART peripherals.
 *
 * @ingroup syn_system
 */

#ifndef SYN_PORT_SERIAL_H
#define SYN_PORT_SERIAL_H

#include "../common/syn_defs.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the console serial port.
 *
 * @param baudrate  Desired baud rate. Pass 0 for default (or if the
 *                  underlying transport ignores baud rate, e.g. USB CDC).
 * @return SYN_OK on success.
 */
SYN_Status syn_port_serial_init(uint32_t baudrate);

/**
 * @brief Write bytes to the console.
 *
 * Writes up to @p len bytes. May briefly block if the transmit buffer
 * is full, but must not spin indefinitely.
 *
 * @param data  Data to write.
 * @param len   Number of bytes.
 * @return Number of bytes actually written, or -1 on error.
 */
int syn_port_serial_write(const uint8_t *data, size_t len);

/**
 * @brief Read available bytes from the console (non-blocking).
 *
 * Returns immediately with however many bytes are available, up to
 * @p max_len. Returns 0 if no data is available. **Never blocks.**
 *
 * @param buf      Buffer to read into.
 * @param max_len  Maximum bytes to read.
 * @return Number of bytes read (0 if nothing available), or -1 on error.
 */
int syn_port_serial_read(uint8_t *buf, size_t max_len);

/* ── Inline convenience helpers (not port obligations) ─────────────────── */

/**
 * @brief Write a single byte to the console.
 * @param byte  Byte to write.
 * @return 1 on success, -1 on error.
 */
static inline int syn_port_serial_write_byte(uint8_t byte)
{
    return syn_port_serial_write(&byte, 1);
}

/**
 * @brief Read a single byte from the console (non-blocking).
 * @param byte  [out] Received byte.
 * @return 1 if a byte was read, 0 if nothing available.
 */
static inline int syn_port_serial_read_byte(uint8_t *byte)
{
    return syn_port_serial_read(byte, 1);
}

#ifdef __cplusplus
}
#endif

#endif /* SYN_PORT_SERIAL_H */
