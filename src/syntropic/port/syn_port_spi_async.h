/**
 * @file syn_port_spi_async.h
 * @brief Async SPI port interface — implement these for your platform.
 *
 * Provides non-blocking, callback-based SPI transfers alongside the
 * existing blocking API in syn_port_spi.h. The callback fires from
 * ISR context.
 *
 * @par Usage
 * @code
 *   static void on_spi_done(uint8_t bus, SYN_Status result, void *ctx) {
 *       syn_port_spi_cs_deassert(0, cs_pin);
 *       // Process rx_buf
 *   }
 *
 *   syn_port_spi_cs_assert(0, cs_pin);
 *   SYN_SPI_Xfer xfer = {
 *       .bus       = 0,
 *       .tx_buf    = tx_data,
 *       .rx_buf    = rx_data,
 *       .len       = 256,
 *       .callback  = on_spi_done,
 *       .user_data = NULL,
 *   };
 *   syn_port_spi_xfer_async(&xfer);
 * @endcode
 * @ingroup syn_system
 */

#ifndef SYN_PORT_SPI_ASYNC_H
#define SYN_PORT_SPI_ASYNC_H

#if __has_include("syn_config.h")
  #include "syn_config.h"
#endif

#if defined(SYN_USE_SPI_ASYNC) && SYN_USE_SPI_ASYNC

#include "../common/syn_defs.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Callback ──────────────────────────────────────────────────────────── */

/**
 * @brief SPI async transfer completion callback.
 *
 * Called from ISR context when the transfer finishes.
 *
 * @param bus     SPI bus index.
 * @param result  SYN_OK on success, SYN_ERROR on failure.
 * @param ctx     User context from the transfer descriptor.
 */
typedef void (*SYN_SPI_Callback)(uint8_t bus, SYN_Status result, void *ctx);

/* ── Transfer descriptor ───────────────────────────────────────────────── */

/**
 * @brief SPI async transfer descriptor — caller-owned.
 *
 * Full-duplex transfer: transmits from tx_buf while receiving into rx_buf.
 * Either buffer may be NULL for half-duplex operation.
 * CS assertion/deassertion is the caller's responsibility.
 */
typedef struct {
    uint8_t          bus;        /**< SPI bus index                      */
    const uint8_t   *tx_buf;     /**< TX buffer (NULL → send zeros)     */
    uint8_t         *rx_buf;     /**< RX buffer (NULL → discard)        */
    size_t           len;        /**< Transfer length in bytes           */
    SYN_SPI_Callback callback;   /**< Called on completion (ISR ctx)     */
    void            *user_data;  /**< User context for callback          */
} SYN_SPI_Xfer;

/* ── Port functions (user implements) ──────────────────────────────────── */

/**
 * @brief Start an asynchronous SPI transfer.
 *
 * The transfer descriptor and its buffers must remain valid until the
 * callback fires.
 *
 * @param xfer  Transfer descriptor.
 * @return SYN_OK if started, SYN_BUSY if the bus is occupied,
 *         SYN_ERROR on invalid parameters.
 */
SYN_Status syn_port_spi_xfer_async(const SYN_SPI_Xfer *xfer);

/**
 * @brief Cancel an in-progress async SPI transfer.
 * @param bus  SPI bus index.
 * @return SYN_OK if cancelled, SYN_ERROR if no transfer in progress.
 */
SYN_Status syn_port_spi_cancel(uint8_t bus);

#ifdef __cplusplus
}
#endif

#endif /* SYN_USE_SPI_ASYNC */

#endif /* SYN_PORT_SPI_ASYNC_H */
