/**
 * @file syn_chacha20poly1305.h
 * @brief ChaCha20-Poly1305 AEAD — RFC 8439, pure C99.
 *
 * Provides the Authenticated Encryption with Associated Data (AEAD)
 * construction used by WireGuard for both handshake and transport
 * encryption. Also exposes raw ChaCha20 for standalone use.
 *
 * All state is caller-owned. No heap.
 *
 * @par Usage
 * @code
 *   uint8_t key[32], nonce[12], tag[16];
 *   uint8_t plaintext[64], ciphertext[64];
 *
 *   // Encrypt:
 *   syn_aead_encrypt(key, nonce, NULL, 0,
 *                    plaintext, 64, ciphertext, tag);
 *
 *   // Decrypt:
 *   bool ok = syn_aead_decrypt(key, nonce, NULL, 0,
 *                              ciphertext, 64, tag, plaintext);
 * @endcode
 * @ingroup syn_crypto
 */

#ifndef SYN_CHACHA20POLY1305_H
#define SYN_CHACHA20POLY1305_H

#if __has_include("syn_config.h")
  #include "syn_config.h"
#endif

#if !defined(SYN_USE_CHACHA20POLY1305) || SYN_USE_CHACHA20POLY1305

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ChaCha20 ───────────────────────────────────────────────────────────── */

/**
 * @brief XOR data with ChaCha20 keystream.
 *
 * Encrypts (or decrypts — same operation) by XOR-ing with the
 * ChaCha20 keystream starting at the given block counter.
 *
 * @param key      256-bit key (32 bytes).
 * @param nonce    96-bit nonce (12 bytes).
 * @param counter  Initial block counter (usually 0 or 1).
 * @param in       Input data.
 * @param len      Data length in bytes.
 * @param out      Output buffer (may alias in).
 */
void syn_chacha20_xor(const uint8_t key[32],
                      const uint8_t nonce[12],
                      uint32_t counter,
                      const uint8_t *in, size_t len,
                      uint8_t *out);

/**
 * @brief Generate ChaCha20 keystream block (no XOR).
 *
 * Produces exactly 64 bytes of keystream for the given counter value.
 *
 * @param key      256-bit key (32 bytes).
 * @param nonce    96-bit nonce (12 bytes).
 * @param counter  Block counter.
 * @param out      Output buffer (exactly 64 bytes).
 */
void syn_chacha20_block(const uint8_t key[32],
                        const uint8_t nonce[12],
                        uint32_t counter,
                        uint8_t out[64]);

/* ── ChaCha20-Poly1305 AEAD ─────────────────────────────────────────────── */

/**
 * @brief Encrypt and authenticate (AEAD).
 *
 * @param key         256-bit key (32 bytes).
 * @param nonce       96-bit nonce (12 bytes).
 * @param aad         Additional authenticated data (or NULL).
 * @param aad_len     AAD length.
 * @param plaintext   Data to encrypt.
 * @param pt_len      Plaintext length.
 * @param ciphertext  Output ciphertext (same length as plaintext).
 * @param tag         Output 128-bit authentication tag (16 bytes).
 */
void syn_aead_encrypt(const uint8_t key[32],
                      const uint8_t nonce[12],
                      const uint8_t *aad, size_t aad_len,
                      const uint8_t *plaintext, size_t pt_len,
                      uint8_t *ciphertext,
                      uint8_t tag[16]);

/**
 * @brief Decrypt and verify (AEAD).
 *
 * @param key         256-bit key (32 bytes).
 * @param nonce       96-bit nonce (12 bytes).
 * @param aad         Additional authenticated data (or NULL).
 * @param aad_len     AAD length.
 * @param ciphertext  Data to decrypt.
 * @param ct_len      Ciphertext length.
 * @param tag         Expected 128-bit authentication tag (16 bytes).
 * @param plaintext   Output plaintext (same length as ciphertext).
 * @return true if tag is valid and decryption succeeded.
 */
bool syn_aead_decrypt(const uint8_t key[32],
                      const uint8_t nonce[12],
                      const uint8_t *aad, size_t aad_len,
                      const uint8_t *ciphertext, size_t ct_len,
                      const uint8_t tag[16],
                      uint8_t *plaintext);

#ifdef __cplusplus
}
#endif

#endif /* SYN_USE_CHACHA20POLY1305 */

#endif /* SYN_CHACHA20POLY1305_H */
