#if __has_include("syn_config.h")
  #include "syn_config.h"
#endif

#if !defined(SYN_USE_SNTP) || SYN_USE_SNTP

/**
 * @file syn_sntp.c
 * @brief SNTP client implementation — RFC 4330 subset.
 */

#include "syn_sntp.h"
#include "../util/syn_assert.h"
#include "../port/syn_port_system.h"
#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────────── */

/** @brief Read a big-endian uint32 from a byte buffer. */
static inline uint32_t load32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8)
         | ((uint32_t)p[3]);
}

/* ── API ────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise SNTP client with server address and sync interval.
 * @param sntp            Caller-owned SNTP context.
 * @param server          NTP server address.
 * @param sync_interval_s Re-sync interval in seconds.
 */
void syn_sntp_init(SYN_SNTP *sntp, const SYN_SockAddr *server,
                   uint32_t sync_interval_s)
{
    SYN_ASSERT(sntp != NULL);
    SYN_ASSERT(server != NULL);

    memset(sntp, 0, sizeof(*sntp));
    sntp->server          = *server;
    sntp->sync_interval_s = sync_interval_s;
    sntp->udp_sock        = SYN_SOCKET_INVALID;
}

/**
 * @brief Send one SNTP query and block until response or timeout.
 * @param sntp SNTP context.
 * @return SYN_OK on success, SYN_TIMEOUT or SYN_ERROR on failure.
 */
SYN_Status syn_sntp_query(SYN_SNTP *sntp)
{
    SYN_ASSERT(sntp != NULL);

    uint8_t pkt[SYN_SNTP_PACKET_SIZE];
    SYN_SockAddr from;
    int n;

    /* Open ephemeral UDP socket */
    SYN_Socket sock = syn_port_udp_open(0);
    if (sock == SYN_SOCKET_INVALID) {
        return SYN_ERROR;
    }

    /* Build SNTP request: LI=0, VN=4, Mode=3 (client) */
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x23; /* 00 100 011 */

    /* Send request */
    n = syn_port_udp_sendto(sock, pkt, sizeof(pkt), &sntp->server);
    if (n != SYN_SNTP_PACKET_SIZE) {
        syn_port_sock_close(sock);
        return SYN_ERROR;
    }

    /* Wait for response */
    n = syn_port_udp_recvfrom(sock, pkt, sizeof(pkt), &from,
                              SYN_SNTP_TIMEOUT_MS);
    syn_port_sock_close(sock);

    if (n < SYN_SNTP_PACKET_SIZE) {
        return SYN_TIMEOUT;
    }

    /* Validate: check Mode == 4 (server) or Mode == 5 (broadcast),
     * and stratum != 0 (kiss-of-death) */
    uint8_t mode = pkt[0] & 0x07;
    if (mode != 4 && mode != 5) {
        return SYN_ERROR;
    }
    if (pkt[1] == 0) {  /* stratum 0 = kiss-of-death */
        return SYN_ERROR;
    }

    /* Extract transmit timestamp (bytes 40–47, NTP epoch big-endian) */
    uint32_t ntp_s    = load32_be(pkt + 40);
    uint32_t ntp_frac = load32_be(pkt + 44);

    /* Convert NTP epoch (1900) → Unix epoch (1970) */
    if (ntp_s < SYN_SNTP_EPOCH_OFFSET) {
        return SYN_ERROR;  /* timestamp before 1970 — invalid */
    }

    sntp->epoch_s     = ntp_s - SYN_SNTP_EPOCH_OFFSET;
    sntp->epoch_frac  = ntp_frac;
    sntp->sync_tick_ms = syn_port_get_tick_ms();
    sntp->synced      = true;

    return SYN_OK;
}

/**
 * @brief Return current Unix epoch seconds (synced time + local drift).
 * @param sntp SNTP context.
 * @return Epoch seconds, or 0 if not synced.
 */
uint32_t syn_sntp_get_epoch_s(const SYN_SNTP *sntp)
{
    if (!sntp->synced) return 0;

    uint32_t elapsed_ms = syn_port_get_tick_ms() - sntp->sync_tick_ms;
    return sntp->epoch_s + (elapsed_ms / 1000u);
}

/**
 * @brief Return sub-second component as nanoseconds.
 * @param sntp SNTP context.
 * @return Nanoseconds (0–999 999 999), or 0 if not synced.
 */
uint32_t syn_sntp_get_epoch_ns(const SYN_SNTP *sntp)
{
    if (!sntp->synced) return 0;

    uint32_t elapsed_ms = syn_port_get_tick_ms() - sntp->sync_tick_ms;
    uint32_t sub_s_ms   = elapsed_ms % 1000u;

    return sub_s_ms * 1000000u;  /* ms → ns */
}

/* ── Non-blocking helpers (for protothread task) ───────────────────────── */

/**
 * @brief Build and send an SNTP request on an already-open socket.
 *
 * @param sntp  SNTP context (for server address).
 * @param sock  Open UDP socket.
 * @return SYN_OK if sent, SYN_ERROR on failure.
 */
static SYN_Status sntp_send_request(const SYN_SNTP *sntp, SYN_Socket sock)
{
    uint8_t pkt[SYN_SNTP_PACKET_SIZE];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x23; /* LI=0, VN=4, Mode=3 (client) */

    int n = syn_port_udp_sendto(sock, pkt, sizeof(pkt), &sntp->server);
    return (n == SYN_SNTP_PACKET_SIZE) ? SYN_OK : SYN_ERROR;
}

/**
 * @brief Non-blocking receive: try to read an NTP response.
 *
 * Calls recvfrom with timeout_ms = 0 (non-blocking). If a valid
 * response is available, parses it and sets sntp->synced = true.
 *
 * @param sntp  SNTP context (updated on success).
 * @param sock  Open UDP socket.
 * @return SYN_OK if synced, SYN_BUSY if no data yet, SYN_ERROR on bad response.
 */
static SYN_Status sntp_try_recv(SYN_SNTP *sntp, SYN_Socket sock)
{
    uint8_t pkt[SYN_SNTP_PACKET_SIZE];
    SYN_SockAddr from;

    int n = syn_port_udp_recvfrom(sock, pkt, sizeof(pkt), &from, 0);
    if (n < SYN_SNTP_PACKET_SIZE) {
        return (n == 0 || n == -1) ? SYN_BUSY : SYN_ERROR;
    }

    /* Validate mode (4=server, 5=broadcast) and stratum != 0 */
    uint8_t mode = pkt[0] & 0x07;
    if (mode != 4 && mode != 5) return SYN_ERROR;
    if (pkt[1] == 0) return SYN_ERROR;  /* kiss-of-death */

    /* Extract transmit timestamp */
    uint32_t ntp_s = load32_be(pkt + 40);
    if (ntp_s < SYN_SNTP_EPOCH_OFFSET) return SYN_ERROR;

    sntp->epoch_s      = ntp_s - SYN_SNTP_EPOCH_OFFSET;
    sntp->epoch_frac   = load32_be(pkt + 44);
    sntp->sync_tick_ms = syn_port_get_tick_ms();
    sntp->synced       = true;

    return SYN_OK;
}

/* ── Protothread task ───────────────────────────────────────────────────── */

/**
 * @brief Protothread task: periodically re-syncs via SNTP.
 *
 * Fully non-blocking — uses timeout_ms = 0 polling so the cooperative
 * scheduler is never blocked. Each query cycle:
 *   1. Open socket
 *   2. Send NTP request
 *   3. Poll for response (PT_WAIT_UNTIL with deadline)
 *   4. Close socket
 *   5. Retry or wait for next sync interval
 *
 * @param pt   Protothread context.
 * @param task Task descriptor (user_data must point to SYN_SNTP).
 * @return PT status.
 */
SYN_PT_Status syn_sntp_task(SYN_PT *pt, SYN_Task *task)
{
    SYN_SNTP *sntp = (SYN_SNTP *)task->user_data;
    SYN_ASSERT(sntp != NULL);

    /* Static locals survive PT_YIELD — safe for single-instance task */
    static uint8_t retries;

    PT_BEGIN(pt);

    for (;;) {
        retries = 0;
        while (retries < SYN_SNTP_MAX_RETRIES) {

            /* Phase 1: Open socket */
            sntp->udp_sock = syn_port_udp_open(0);
            if (sntp->udp_sock == SYN_SOCKET_INVALID) {
                retries++;
                PT_TASK_DELAY_MS(pt, task, 1000);
                continue;
            }

            /* Phase 2: Send request */
            if (sntp_send_request(sntp, sntp->udp_sock) != SYN_OK) {
                syn_port_sock_close(sntp->udp_sock);
                sntp->udp_sock = SYN_SOCKET_INVALID;
                retries++;
                PT_TASK_DELAY_MS(pt, task, 1000);
                continue;
            }

            /* Phase 3: Non-blocking poll with deadline */
            sntp->recv_deadline = syn_port_get_tick_ms() + SYN_SNTP_TIMEOUT_MS;
            PT_WAIT_UNTIL(pt,
                sntp_try_recv(sntp, sntp->udp_sock) != SYN_BUSY ||
                (int32_t)(syn_port_get_tick_ms() - sntp->recv_deadline) >= 0);

            /* Phase 4: Close socket */
            syn_port_sock_close(sntp->udp_sock);
            sntp->udp_sock = SYN_SOCKET_INVALID;

            if (sntp->synced) break;  /* Success — exit retry loop */
            retries++;
            PT_TASK_DELAY_MS(pt, task, 1000);  /* 1s between retries */
        }

        /* Wait for next sync interval */
        PT_TASK_DELAY_MS(pt, task, sntp->sync_interval_s * 1000u);
    }

    PT_END(pt);
}

#endif /* SYN_USE_SNTP */
