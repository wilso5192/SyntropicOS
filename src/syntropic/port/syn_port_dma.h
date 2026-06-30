/**
 * @file syn_port_dma.h
 * @brief DMA port interface — implement these for your platform.
 *
 * Provides a portable DMA abstraction for memory-to-peripheral,
 * peripheral-to-memory, and memory-to-memory transfers. The completion
 * callback fires from ISR context — use syn_workqueue_post() to defer
 * heavy processing to the main context.
 *
 * @par Usage
 * @code
 *   static void on_dma_done(uint8_t ch, SYN_Status result, void *ctx) {
 *       // Transfer complete — process data or post to workqueue
 *   }
 *
 *   SYN_DMA_Config cfg = {
 *       .channel   = 0,
 *       .direction = SYN_DMA_PERIPH_TO_MEM,
 *       .width     = SYN_DMA_WIDTH_16,
 *       .src_incr  = false,
 *       .dst_incr  = true,
 *       .callback  = on_dma_done,
 *       .user_data = NULL,
 *   };
 *   syn_port_dma_init(&cfg);
 *   syn_port_dma_start(0, &ADC_DR, adc_buf, 256);
 * @endcode
 * @ingroup syn_system
 */

#ifndef SYN_PORT_DMA_H
#define SYN_PORT_DMA_H

#if __has_include("syn_config.h")
  #include "syn_config.h"
#endif

#if !defined(SYN_USE_DMA) || SYN_USE_DMA

#include "../common/syn_defs.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Transfer direction ────────────────────────────────────────────────── */

/** @brief DMA transfer direction. */
typedef enum {
    SYN_DMA_MEM_TO_PERIPH = 0,  /**< Memory → peripheral          */
    SYN_DMA_PERIPH_TO_MEM = 1,  /**< Peripheral → memory          */
    SYN_DMA_MEM_TO_MEM    = 2,  /**< Memory → memory              */
} SYN_DMA_Dir;

/* ── Data width ────────────────────────────────────────────────────────── */

/** @brief DMA element data width. */
typedef enum {
    SYN_DMA_WIDTH_8  = 1,  /**< 8-bit elements   */
    SYN_DMA_WIDTH_16 = 2,  /**< 16-bit elements  */
    SYN_DMA_WIDTH_32 = 4,  /**< 32-bit elements  */
} SYN_DMA_Width;

/* ── Callback ──────────────────────────────────────────────────────────── */

/**
 * @brief DMA transfer completion callback.
 *
 * Called from ISR context when a transfer completes or encounters
 * an error. Use syn_workqueue_post() for non-trivial processing.
 *
 * @param channel  DMA channel that completed.
 * @param result   SYN_OK on success, SYN_ERROR on transfer error.
 * @param ctx      User context provided in the config.
 */
typedef void (*SYN_DMA_Callback)(uint8_t channel, SYN_Status result, void *ctx);

/* ── Channel configuration ─────────────────────────────────────────────── */

/** @brief DMA channel configuration — caller-owned. */
typedef struct {
    uint8_t          channel;    /**< DMA channel number               */
    SYN_DMA_Dir      direction;  /**< Transfer direction               */
    SYN_DMA_Width    width;      /**< Source/dest data width            */
    bool             src_incr;   /**< Increment source address?        */
    bool             dst_incr;   /**< Increment destination address?   */
    SYN_DMA_Callback callback;   /**< Completion callback (ISR ctx)    */
    void            *user_data;  /**< User context for callback        */
} SYN_DMA_Config;

/* ── Port functions (user implements) ──────────────────────────────────── */

/**
 * @brief Initialize a DMA channel.
 * @param cfg  Channel configuration.
 * @return SYN_OK on success.
 */
SYN_Status syn_port_dma_init(const SYN_DMA_Config *cfg);

/**
 * @brief Start a DMA transfer.
 *
 * @param channel  DMA channel number (must have been initialized).
 * @param src      Source address (memory or peripheral register).
 * @param dst      Destination address (memory or peripheral register).
 * @param count    Number of elements (not bytes) to transfer.
 * @return SYN_OK on success, SYN_ERROR if channel busy.
 */
SYN_Status syn_port_dma_start(uint8_t channel,
                               const volatile void *src,
                               volatile void *dst,
                               size_t count);

/**
 * @brief Stop a DMA transfer in progress.
 * @param channel  DMA channel to stop.
 * @return SYN_OK on success.
 */
SYN_Status syn_port_dma_stop(uint8_t channel);

/**
 * @brief Check if a DMA channel is currently transferring.
 * @param channel  DMA channel to query.
 * @return true if a transfer is in progress.
 */
bool syn_port_dma_busy(uint8_t channel);

/**
 * @brief Get the number of remaining elements in the current transfer.
 * @param channel  DMA channel to query.
 * @return Remaining element count, or 0 if idle.
 */
size_t syn_port_dma_remaining(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* SYN_USE_DMA */

#endif /* SYN_PORT_DMA_H */
