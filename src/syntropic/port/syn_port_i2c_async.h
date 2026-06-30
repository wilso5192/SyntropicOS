/**
 * @file syn_port_i2c_async.h
 * @brief Async I2C port interface — implement these for your platform.
 *
 * Provides non-blocking, callback-based I2C transactions alongside
 * the existing blocking API in syn_port_i2c.h. The callback fires
 * from ISR context.
 *
 * @par Usage
 * @code
 *   static void on_i2c_done(uint8_t bus, SYN_Status result, void *ctx) {
 *       // Access rx_buf — data is ready
 *   }
 *
 *   uint8_t reg = 0xD0;
 *   uint8_t val;
 *   SYN_I2C_Xfer xfer = {
 *       .bus       = 0,
 *       .addr      = 0x76,
 *       .tx_data   = &reg,
 *       .tx_len    = 1,
 *       .rx_data   = &val,
 *       .rx_len    = 1,
 *       .callback  = on_i2c_done,
 *       .user_data = NULL,
 *   };
 *   syn_port_i2c_xfer_async(&xfer);
 * @endcode
 * @ingroup syn_system
 */

#ifndef SYN_PORT_I2C_ASYNC_H
#define SYN_PORT_I2C_ASYNC_H

#if __has_include("syn_config.h")
  #include "syn_config.h"
#endif

#if defined(SYN_USE_I2C_ASYNC) && SYN_USE_I2C_ASYNC

#include "../common/syn_defs.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Callback ──────────────────────────────────────────────────────────── */

/**
 * @brief I2C async transaction completion callback.
 *
 * Called from ISR context when the transaction finishes.
 *
 * @param bus     I2C bus index.
 * @param result  SYN_OK on ACK, SYN_ERROR on NACK/timeout.
 * @param ctx     User context from the transfer descriptor.
 */
typedef void (*SYN_I2C_Callback)(uint8_t bus, SYN_Status result, void *ctx);

/* ── Transfer descriptor ───────────────────────────────────────────────── */

/**
 * @brief I2C async transfer descriptor — caller-owned.
 *
 * Describes a write-then-read I2C transaction. Set tx_data/tx_len to
 * zero for a pure read, or rx_data/rx_len to zero for a pure write.
 */
typedef struct {
    uint8_t          bus;        /**< I2C bus index                      */
    uint8_t          addr;       /**< 7-bit device address               */
    const uint8_t   *tx_data;    /**< Write buffer (NULL if read-only)   */
    size_t           tx_len;     /**< Write length (0 for read-only)     */
    uint8_t         *rx_data;    /**< Read buffer (NULL if write-only)   */
    size_t           rx_len;     /**< Read length (0 for write-only)     */
    SYN_I2C_Callback callback;   /**< Called on completion (ISR ctx)     */
    void            *user_data;  /**< User context for callback          */
} SYN_I2C_Xfer;

/* ── Port functions (user implements) ──────────────────────────────────── */

/**
 * @brief Start an asynchronous I2C transaction.
 *
 * The transfer descriptor must remain valid until the callback fires.
 *
 * @param xfer  Transfer descriptor.
 * @return SYN_OK if the transfer was started, SYN_BUSY if the bus is
 *         occupied, SYN_ERROR on invalid parameters.
 */
SYN_Status syn_port_i2c_xfer_async(const SYN_I2C_Xfer *xfer);

/**
 * @brief Cancel an in-progress async I2C transaction.
 * @param bus  I2C bus index.
 * @return SYN_OK if cancelled, SYN_ERROR if no transfer in progress.
 */
SYN_Status syn_port_i2c_cancel(uint8_t bus);

#ifdef __cplusplus
}
#endif

#endif /* SYN_USE_I2C_ASYNC */

#endif /* SYN_PORT_I2C_ASYNC_H */
