# Crypto & VPN

## Cryptographic Primitives

All cryptographic modules are **pure C99**, **zero-allocation**, and **constant-time** where required for security.
Every function operates on caller-owned buffers — no internal state beyond what is passed in.

| Module | Header | Config | Description |
|---|---|---|---|
| BLAKE2s | `crypto/syn_blake2s.h` | `SYN_USE_BLAKE2S` | BLAKE2s hash (RFC 7693) — streaming API with keyed MAC support |
| ChaCha20-Poly1305 | `crypto/syn_chacha20poly1305.h` | `SYN_USE_CHACHA20POLY1305` | ChaCha20 stream cipher + Poly1305 AEAD (RFC 8439) |
| X25519 | `crypto/syn_x25519.h` | `SYN_USE_X25519` | Curve25519 Diffie-Hellman key exchange (RFC 7748) |

### BLAKE2s

A compact implementation of BLAKE2s-256 per RFC 7693. Provides both one-shot and streaming (init/update/final) APIs plus keyed-MAC mode for HMAC-like authentication without HMAC overhead.

```c
#include "syntropic/crypto/syn_blake2s.h"

/* One-shot hash */
uint8_t digest[32];
syn_blake2s("hello", 5, digest, 32);

/* Streaming */
SYN_BLAKE2s ctx;
syn_blake2s_init(&ctx, 32);
syn_blake2s_update(&ctx, buf1, len1);
syn_blake2s_update(&ctx, buf2, len2);
syn_blake2s_final(&ctx, digest);

/* Keyed MAC (e.g. 16-byte tag) */
SYN_BLAKE2s mac_ctx;
syn_blake2s_init_keyed(&mac_ctx, key, 32, 16);
syn_blake2s_update(&mac_ctx, message, msg_len);
syn_blake2s_final(&mac_ctx, tag);
```

Used internally by the WireGuard module for HKDF and MAC1 computations.

---

### ChaCha20-Poly1305

Authenticated Encryption with Associated Data (AEAD) per RFC 8439.
Provides individual ChaCha20 keystream and XOR functions, plus the combined AEAD construction.

```c
#include "syntropic/crypto/syn_chacha20poly1305.h"

uint8_t key[32], nonce[12];
uint8_t plaintext[128], ciphertext[128], tag[16];

/* Encrypt + authenticate */
syn_aead_encrypt(key, nonce,
                 aad, aad_len,           /* additional authenticated data */
                 plaintext, sizeof(plaintext),
                 ciphertext, tag);

/* Decrypt + verify (returns false on tamper) */
bool ok = syn_aead_decrypt(key, nonce,
                            aad, aad_len,
                            ciphertext, sizeof(ciphertext),
                            tag, plaintext);
```

!!! warning "Nonce reuse"
    Never reuse a nonce with the same key. The WireGuard module handles nonces automatically via monotonic counters.

---

### X25519

Curve25519 elliptic-curve Diffie-Hellman key exchange per RFC 7748.
Uses a Montgomery ladder over GF(2²⁵⁵ − 19) with 16 × 16-bit limbs.

```c
#include "syntropic/crypto/syn_x25519.h"

/* Generate a keypair */
uint8_t private_key[32], public_key[32];
/* Fill private_key from a CSPRNG, then: */
syn_x25519_clamp(private_key);
syn_x25519_pubkey(public_key, private_key);

/* Compute shared secret */
uint8_t shared[32];
syn_x25519(shared, my_private_key, peer_public_key);
```

!!! note "Clamping"
    `syn_x25519_clamp()` sets bits per RFC 7748 §5. Call it once on raw key material before deriving the public key. The WireGuard module handles this internally.

---

## WireGuard VPN Client

| Module | Header | Config | Description |
|---|---|---|---|
| WireGuard | `net/syn_wg.h` | `SYN_USE_WG` | WireGuard tunnel client — Noise IKpsk2, cooperative, zero-allocation |

### Overview

A full WireGuard client implementing the Noise_IKpsk2 handshake pattern with preshared key support.
Runs as a cooperative protothread task and handles:

- Noise IKpsk2 handshake (initiator role)
- ChaCha20-Poly1305 transport encryption/decryption
- Anti-replay sliding window (32-bit bitmap)
- Persistent keepalive
- Automatic rekeying (every 120s)
- Session timeout and re-handshake (after 180s)
- MAC1 cookie authentication

### Dependencies

The WireGuard module requires all three crypto primitives plus SNTP:

```
SYN_USE_WG → SYN_USE_BLAKE2S
           → SYN_USE_CHACHA20POLY1305
           → SYN_USE_X25519
           → SYN_USE_SNTP
```

### Usage

```c
#include "syntropic/net/syn_wg.h"

/* Buffers — caller-owned, sized for MTU + overhead */
static uint8_t wg_rx[SYN_WG_MTU + SYN_WG_TRANSPORT_OVERHEAD];
static uint8_t wg_tx[SYN_WG_MTU + SYN_WG_TRANSPORT_OVERHEAD];

/* Configuration */
static const SYN_WgConfig wg_cfg = {
    .private_key     = { /* 32 bytes from wg genkey */ },
    .peer_public_key = { /* 32 bytes: server's public key */ },
    .preshared_key   = { /* 32 bytes: optional PSK, zero if unused */ },
    .endpoint        = { .ip = SYN_IP4(203,0,113,1), .port = 51820 },
    .keepalive_interval_s = 25,
};

/* Initialise */
static SYN_WG wg;
syn_wg_init(&wg, &wg_cfg, &sntp,
            wg_rx, sizeof(wg_rx),
            wg_tx, sizeof(wg_tx));

/* Set receive callback for decrypted IP packets */
wg.on_recv = my_ip_packet_handler;
wg.on_recv_ctx = &my_context;

/* Register as a scheduler task (after SNTP) */
syn_task_create(&tasks[1], "wg", syn_wg_task, 0, &wg);
```

### Sending Packets

```c
/* Send a raw IP packet through the tunnel */
SYN_Status status = syn_wg_send(&wg, ip_packet, ip_len);

/* Check connection state */
if (syn_wg_is_established(&wg)) {
    /* Tunnel is up */
}
```

### Protocol Details

| Constant | Value | Description |
|---|---|---|
| `SYN_WG_MTU` | 1420 | Inner IP MTU before encryption |
| `SYN_WG_TRANSPORT_OVERHEAD` | 32 | type(4) + receiver(4) + counter(8) + tag(16) |
| `SYN_WG_REKEY_AFTER_TIME` | 120 s | Initiate rekey after this duration |
| `SYN_WG_REJECT_AFTER_TIME` | 180 s | Drop session after this duration |
| `SYN_WG_REKEY_TIMEOUT` | 5 s | Retry handshake if no response |
| `SYN_WG_KEEPALIVE_TIMEOUT` | 10 s | Send keepalive if no outbound traffic |

### Handshake Flow

The client implements the initiator side of the Noise IK pattern with preshared key:

```
Initiator (us)                          Responder (server)
     │                                       │
     │──── Handshake Initiation (148 B) ────▶│
     │     type(4) sender(4)                 │
     │     ephemeral(32)                     │
     │     encrypted_static(48)              │
     │     encrypted_timestamp(28)           │
     │     mac1(16) mac2(16)                 │
     │                                       │
     │◀─── Handshake Response (92 B) ────────│
     │     type(4) sender(4) receiver(4)     │
     │     ephemeral(32)                     │
     │     encrypted_nothing(16)             │
     │     mac1(16) mac2(16)                 │
     │                                       │
     │◀───▶ Transport Data ◀───▶│
     │     type(4) receiver(4) counter(8)    │
     │     encrypted_packet(N) tag(16)       │
```

### States

| State | Enum | Description |
|---|---|---|
| Disconnected | `SYN_WG_DISCONNECTED` | No session, no handshake in progress |
| Handshake Init | `SYN_WG_HANDSHAKE_INIT` | Initiation sent, waiting for response |
| Established | `SYN_WG_ESTABLISHED` | Session active, transport data flowing |

!!! tip "Interop"
    This client is tested against standard WireGuard servers (Linux kernel, wireguard-go, and router implementations). Any standard `wg` peer configuration will work.
