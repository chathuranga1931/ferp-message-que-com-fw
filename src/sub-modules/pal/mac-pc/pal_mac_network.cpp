/**
 * @file pal_mac_network.cpp
 * @brief PAL network implementation for macOS (simulator).
 *
 * pal_network_ping() probes 8.8.8.8:53 (Google DNS) via a TCP connect
 * with a short timeout.  This avoids the root privilege requirement of
 * raw ICMP on macOS, and matches the approach used by browsers.
 *
 * pal_network_get_ip_address() returns a fixed loopback string in the
 * simulator — the real IP is carried in MsgWifiEvent payloads, not the
 * OS network stack, so this stub is only needed to satisfy the linker.
 */

#include "pal_network.h"
#include "pal_logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>

#define __TAG__  "PAL_NET "

#define NETW_LOG  true

// ---------------------------------------------------------------------------
// pal_network_ping
// ---------------------------------------------------------------------------
//
// Strategy: open a non-blocking TCP socket and attempt to connect to
// host:53 (DNS port — always open on 8.8.8.8).  If the SYN-ACK arrives
// within timeout_ms we declare reachability; otherwise unreachable.

bool pal_network_ping(const char *host, uint32_t timeout_ms)
{
    if (!host) return false;
    if (timeout_ms == 0) timeout_ms = 2000;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_MSG_ERROR(NETW_LOG, "socket() failed: %s", strerror(errno));
        return false;
    }

    // Put socket into non-blocking mode so we can implement a timeout
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(53);
    addr.sin_addr.s_addr = inet_addr(host);

    int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    bool reachable = false;

    if (ret == 0) {
        // Instant connect (very fast local network or loopback)
        reachable = true;
    } else if (errno == EINPROGRESS) {
        // Non-blocking connect in progress — wait with select()
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);

        struct timeval tv{};
        tv.tv_sec  = (long)(timeout_ms / 1000);
        tv.tv_usec = (long)((timeout_ms % 1000) * 1000);

        int sel = select(sock + 1, nullptr, &wfds, nullptr, &tv);
        if (sel > 0) {
            // Check that the connect actually succeeded
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
            reachable = (err == 0);
        }
        // sel == 0 → timeout; sel < 0 → error → reachable stays false
    }

    close(sock);

    LOG_MSG_INFO(NETW_LOG, "ping %s -> %s", host, reachable ? "OK" : "FAIL");
    return reachable;
}

// ---------------------------------------------------------------------------
// pal_network_ping_detailed  (stub — not used in simulator)
// ---------------------------------------------------------------------------

int32_t pal_network_ping_detailed(const pal_ping_config_t *config,
                                   pal_ping_result_t       *result)
{
    if (!config) return -1;
    bool ok = pal_network_ping(config->target_host,
                                config->timeout_ms ? config->timeout_ms : 2000);
    if (result) {
        result->success          = ok;
        result->packets_sent     = 1;
        result->packets_received = ok ? 1 : 0;
        result->packets_lost     = ok ? 0 : 1;
    }
    return ok ? 0 : -1;
}

// ---------------------------------------------------------------------------
// pal_network_is_connected  (stub — always true in simulator context)
// ---------------------------------------------------------------------------

int32_t pal_network_is_connected(bool *is_connected)
{
    if (is_connected) *is_connected = true;
    return 0;
}

// ---------------------------------------------------------------------------
// pal_network_get_ip_address  (stub — real IP comes from MsgWifiEvent)
// ---------------------------------------------------------------------------

int32_t pal_network_get_ip_address(char *ip_buffer, size_t buffer_size)
{
    if (!ip_buffer || buffer_size == 0) return -1;
    strncpy(ip_buffer, "127.0.0.1", buffer_size - 1);
    ip_buffer[buffer_size - 1] = '\0';
    return 0;
}
