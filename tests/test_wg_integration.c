/**
 * @file test_wg_integration.c
 * @brief Integration test — connects to a real WireGuard server.
 */

#define _POSIX_C_SOURCE 200809L

/*
 * This is a POSIX test harness that:
 *   4. Sends a ping through the tunnel
 *
 * Build:
 *   gcc -Wall -Wextra -std=c99 -I src \
 *       tests/test_wg_integration.c \
 *       src/syntropic/crypto/syn_blake2s.c \
 *       src/syntropic/crypto/syn_chacha20poly1305.c \
 *       src/syntropic/crypto/syn_x25519.c \
 *       src/syntropic/net/syn_sntp.c \
 *       src/syntropic/net/syn_wg.c \
 *       -o test_wg_integration
 *
 * Usage:
 *   # First, start a WireGuard server (see setup_wg_test.sh)
 *   ./test_wg_integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* POSIX sockets */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  POSIX port implementations
 * ═══════════════════════════════════════════════════════════════════════════ */

#include "syntropic/common/syn_defs.h"
#include "syntropic/port/syn_port_socket.h"
#include "syntropic/port/syn_port_system.h"

void syn_assert_failed(const char *file, int line)
{
    fprintf(stderr, "ASSERT FAILED: %s:%d\n", file, line);
    abort();
}

uint32_t syn_port_get_tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void syn_port_delay_ms(uint32_t ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

void syn_port_enter_critical(void) {}
void syn_port_exit_critical(void) {}

/* ── UDP socket port ────────────────────────────────────────────────────── */

SYN_Socket syn_port_udp_open(uint16_t port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return SYN_SOCKET_INVALID;

    if (port > 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(sock);
            return SYN_SOCKET_INVALID;
        }
    }

    return (SYN_Socket)sock;
}

int syn_port_udp_sendto(SYN_Socket sock, const void *data, size_t len,
                        const SYN_SockAddr *to)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(to->port);
    memcpy(&addr.sin_addr.s_addr, to->ip, 4);

    ssize_t n = sendto(sock, data, len, 0,
                       (struct sockaddr *)&addr, sizeof(addr));
    return (int)n;
}

int syn_port_udp_recvfrom(SYN_Socket sock, void *buf, size_t max_len,
                          SYN_SockAddr *from, uint32_t timeout_ms)
{
    if (timeout_ms > 0) {
        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        int ret = poll(&pfd, 1, (int)timeout_ms);
        if (ret <= 0) return (ret == 0) ? 0 : -1;
    } else {
        /* Non-blocking check */
        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        int ret = poll(&pfd, 1, 0);
        if (ret <= 0) return 0;
    }

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    ssize_t n = recvfrom(sock, buf, max_len, 0,
                         (struct sockaddr *)&addr, &addrlen);
    if (n < 0) return -1;

    if (from != NULL) {
        memcpy(from->ip, &addr.sin_addr.s_addr, 4);
        from->port = ntohs(addr.sin_port);
    }
    return (int)n;
}

void syn_port_sock_close(SYN_Socket sock)
{
    close(sock);
}

/* Stubs for unused TCP functions (satisfy linker) */
SYN_Socket syn_port_sock_connect(const SYN_SockAddr *a) { (void)a; return -1; }
SYN_Socket syn_port_sock_connect_host(const char *h, uint16_t p) { (void)h; (void)p; return -1; }
int syn_port_sock_send(SYN_Socket s, const void *d, size_t l) { (void)s; (void)d; (void)l; return -1; }
int syn_port_sock_send_all(SYN_Socket s, const void *d, size_t l) { (void)s; (void)d; (void)l; return -1; }
int syn_port_sock_recv(SYN_Socket s, void *b, size_t l, uint32_t t) { (void)s; (void)b; (void)l; (void)t; return -1; }
SYN_Socket syn_port_sock_listen(uint16_t p, int b) { (void)p; (void)b; return -1; }
SYN_Socket syn_port_sock_accept(SYN_Socket l, uint32_t t) { (void)l; (void)t; return -1; }
SYN_Status syn_port_udp_join_multicast(SYN_Socket s, const char *ip) { (void)s; (void)ip; return SYN_ERROR; }

/* ═══════════════════════════════════════════════════════════════════════════
 *  Include the modules under test
 * ═══════════════════════════════════════════════════════════════════════════ */

#include "syntropic/crypto/syn_blake2s.h"
#include "syntropic/crypto/syn_chacha20poly1305.h"
#include "syntropic/crypto/syn_x25519.h"
#include "syntropic/net/syn_sntp.h"
#include "syntropic/net/syn_wg.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  Base64 decoder (for reading wg keys)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode(const char *in, uint8_t *out, size_t max_out)
{
    size_t len = strlen(in);
    size_t i, j = 0;
    uint32_t buf = 0;
    int bits = 0;

    for (i = 0; i < len; i++) {
        if (in[i] == '=') break;
        int v = b64_val(in[i]);
        if (v < 0) continue;
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (j < max_out) out[j++] = (uint8_t)(buf >> bits);
        }
    }
    return (int)j;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Test runner
 * ═══════════════════════════════════════════════════════════════════════════ */

static void print_hex(const char *label, const uint8_t *data, size_t len)
{
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

static void on_wg_recv(const uint8_t *ip_packet, size_t len, void *ctx)
{
    (void)ctx;
    printf("[WG] Received %zu bytes of decrypted data:\n", len);

    /* Check if it's an ICMP echo reply (IP proto 1, type 0) */
    if (len >= 20 && ip_packet[9] == 1) {
        printf("[WG] Got ICMP packet! type=%u code=%u\n",
               ip_packet[20], ip_packet[21]);
    }

    /* Hex dump first 64 bytes */
    size_t dump = len < 64 ? len : 64;
    for (size_t i = 0; i < dump; i++) {
        printf("%02x ", ip_packet[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (dump % 16 != 0) printf("\n");
}

int main(int argc, char **argv)
{
    printf("=== WireGuard Integration Test ===\n\n");

    /* ── Configuration ──────────────────────────────────────────────── */

    /* These keys must match your WireGuard server config.
     * Generate with: wg genkey | tee privatekey | wg pubkey > publickey */
    const char *client_priv_b64 = NULL;
    const char *server_pub_b64  = NULL;
    const char *psk_b64         = NULL;
    const char *server_ip       = "127.0.0.1";
    uint16_t    server_port     = 51820;
    const char *ntp_ip          = "216.239.35.0";  /* time.google.com */

    /* Parse command line */
    if (argc < 3) {
        printf("Usage: %s <client_priv_b64> <server_pub_b64> "
               "[server_ip] [server_port] [psk_b64]\n\n", argv[0]);
        printf("  ./%s CLIENT_KEY SERVER_PUB 192.168.76.1 51820 PSK_KEY\n", argv[0]);
        return 1;
    }

    client_priv_b64 = argv[1];
    server_pub_b64  = argv[2];
    if (argc > 3) server_ip   = argv[3];
    if (argc > 4) server_port = (uint16_t)atoi(argv[4]);
    if (argc > 5) psk_b64     = argv[5];

    /* Decode keys */
    uint8_t client_priv[32], server_pub[32];
    if (base64_decode(client_priv_b64, client_priv, 32) != 32) {
        fprintf(stderr, "Invalid client private key\n");
        return 1;
    }
    if (base64_decode(server_pub_b64, server_pub, 32) != 32) {
        fprintf(stderr, "Invalid server public key\n");
        return 1;
    }

    uint8_t psk[32];
    memset(psk, 0, 32);
    if (psk_b64 != NULL) {
        if (base64_decode(psk_b64, psk, 32) != 32) {
            fprintf(stderr, "Invalid preshared key\n");
            return 1;
        }
        printf("Using preshared key: yes\n");
    }

    uint8_t client_pub[32];
    syn_x25519_pubkey(client_pub, client_priv);

    print_hex("Client public key", client_pub, 32);
    print_hex("Server public key", server_pub, 32);
    printf("Server endpoint:   %s:%u\n\n", server_ip, server_port);

    /* ── Step 1: SNTP time sync ─────────────────────────────────────── */

    printf("[SNTP] Syncing time from %s...\n", ntp_ip);

    SYN_SNTP sntp;
    SYN_SockAddr ntp_addr;
    inet_pton(AF_INET, ntp_ip, ntp_addr.ip);
    ntp_addr.port = 123;

    syn_sntp_init(&sntp, &ntp_addr, 3600);

    SYN_Status st = syn_sntp_query(&sntp);
    if (st != SYN_OK) {
        fprintf(stderr, "[SNTP] FAILED (status=%d). Cannot proceed without time.\n", st);
        fprintf(stderr, "       Make sure you have network access to %s:123\n", ntp_ip);
        return 1;
    }

    uint32_t epoch = syn_sntp_get_epoch_s(&sntp);
    printf("[SNTP] Synced! Epoch: %u (", epoch);
    time_t t = (time_t)epoch;
    printf("%s", ctime(&t));
    printf(")\n\n");

    /* ── Step 2: WireGuard handshake ────────────────────────────────── */

    printf("[WG] Initiating handshake with %s:%u...\n", server_ip, server_port);

    static uint8_t rx_buf[2048], tx_buf[2048];
    SYN_WG wg;
    SYN_WgConfig cfg;

    memcpy(cfg.private_key, client_priv, 32);
    memcpy(cfg.peer_public_key, server_pub, 32);
    memcpy(cfg.preshared_key, psk, 32);
    inet_pton(AF_INET, server_ip, cfg.endpoint.ip);
    cfg.endpoint.port = server_port;
    cfg.keepalive_interval_s = 25;

    syn_wg_init(&wg, &cfg, &sntp, rx_buf, sizeof(rx_buf), tx_buf, sizeof(tx_buf));
    wg.on_recv = on_wg_recv;

    /* Drive the state machine manually (no scheduler in this test) */
    SYN_PT pt;
    SYN_Task task;
    memset(&pt, 0, sizeof(pt));
    memset(&task, 0, sizeof(task));
    task.user_data = &wg;

    /* Run iterations until established or timeout */
    uint32_t start = syn_port_get_tick_ms();
    int iter = 0;

    while (!syn_wg_is_established(&wg)) {
        syn_wg_task(&pt, &task);

        uint32_t elapsed = syn_port_get_tick_ms() - start;
        if (elapsed > 10000) {
            fprintf(stderr, "[WG] Handshake timeout after 10 seconds\n");
            fprintf(stderr, "     State: %d\n", wg.state);
            return 1;
        }

        syn_port_delay_ms(10);
        iter++;
    }

    printf("[WG] *** TUNNEL ESTABLISHED *** (after %d iterations, %u ms)\n\n",
           iter, syn_port_get_tick_ms() - start);

    /* ── Step 3: Send a ping through the tunnel ─────────────────────── */

    printf("[WG] Sending ICMP echo request to 172.17.2.1...\n");

    /* Build a minimal ICMP echo request inside an IPv4 packet */
    uint8_t ping[28] = {
        /* IPv4 header (20 bytes) */
        0x45, 0x00, 0x00, 0x1C,  /* ver+ihl, dscp, total length = 28 */
        0x00, 0x01, 0x00, 0x00,  /* id, flags+offset */
        0x40, 0x01, 0x00, 0x00,  /* ttl=64, proto=ICMP, checksum (computed below) */
        0xAC, 0x11, 0x02, 0x09,  /* src: 172.17.2.9 */
        0xAC, 0x11, 0x02, 0x01,  /* dst: 172.17.2.1 */
        /* ICMP echo request (8 bytes) */
        0x08, 0x00, 0x00, 0x00,  /* type=echo, code=0, checksum */
        0x00, 0x01, 0x00, 0x01,  /* id=1, seq=1 */
    };

    /* Compute IP header checksum */
    {
        uint32_t sum = 0;
        for (int i = 0; i < 20; i += 2)
            sum += ((uint32_t)ping[i] << 8) | ping[i+1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        uint16_t cksum = (uint16_t)~sum;
        ping[10] = (uint8_t)(cksum >> 8);
        ping[11] = (uint8_t)(cksum);
    }

    /* Compute ICMP checksum */
    {
        uint32_t sum = 0;
        for (int i = 20; i < 28; i += 2)
            sum += ((uint32_t)ping[i] << 8) | ping[i+1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        uint16_t cksum = (uint16_t)~sum;
        ping[22] = (uint8_t)(cksum >> 8);
        ping[23] = (uint8_t)(cksum);
    }

    st = syn_wg_send(&wg, ping, sizeof(ping));
    printf("[WG] Send result: %s\n", st == SYN_OK ? "OK" : "FAILED");

    /* Poll for response */
    printf("[WG] Waiting for response (3 seconds)...\n");
    start = syn_port_get_tick_ms();
    while (syn_port_get_tick_ms() - start < 3000) {
        syn_wg_task(&pt, &task);
        syn_port_delay_ms(10);
    }

    printf("\n[WG] Test complete.\n");

    return 0;
}
