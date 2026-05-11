/**
 * @file pal_mac_mqtt.cpp
 * @brief macOS PAL implementation of pal_mqtt.h
 *
 * Implements MQTT 3.1.1 over POSIX TCP sockets.
 * No external dependencies beyond BSD sockets + C++17.
 *
 * Supported:
 *   CONNECT / CONNACK
 *   SUBSCRIBE / SUBACK
 *   UNSUBSCRIBE / UNSUBACK
 *   PUBLISH (QoS 0 & 1) / PUBACK
 *   PINGREQ / PINGRESP
 *   DISCONNECT
 *   Auto-reconnect on socket error / broker close
 */

#include "pal_mqtt.h"
#include "pal_logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>

#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#define __TAG__ "PAL_MQTT"

// ---------------------------------------------------------------------------
// Internal client state
// ---------------------------------------------------------------------------

typedef struct {
    pal_mqtt_config_t          config;
    pal_mqtt_event_callback_t  callback;
    void                      *user_data;

    int                        fd;           ///< TCP socket fd; -1 = not connected
    std::atomic<bool>          is_connected;
    std::atomic<bool>          started;      ///< true after pal_mqtt_client_start()
    std::atomic<bool>          should_stop;

    std::thread                conn_thread;  ///< connection + recv loop thread
    std::mutex                 send_mutex;   ///< serialise writes to socket

    std::atomic<uint16_t>      next_msg_id;

    char     host[256];   ///< resolved from broker_uri
    uint16_t port;        ///< resolved from broker_uri / config.port
} mac_mqtt_client_t;

// ---------------------------------------------------------------------------
// Packet utilities
// ---------------------------------------------------------------------------

/** Encode MQTT variable-length integer; return bytes written (1-4). */
static int write_varlen(uint8_t *buf, uint32_t value)
{
    int n = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value > 0) byte |= 0x80;
        buf[n++] = byte;
    } while (value > 0 && n < 4);
    return n;
}

/** Write a length-prefixed UTF-8 string; return bytes written (2 + strlen). */
static int write_str(uint8_t *buf, const char *str)
{
    size_t slen = str ? strlen(str) : 0;
    buf[0] = (uint8_t)((slen >> 8) & 0xFF);
    buf[1] = (uint8_t)(slen & 0xFF);
    if (slen > 0) memcpy(buf + 2, str, slen);
    return (int)(2 + slen);
}

/** Blocking send of exactly len bytes. Caller must hold send_mutex. */
static int sock_send_locked(int fd, const uint8_t *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/** Thread-safe send (acquires send_mutex). */
static int sock_send(mac_mqtt_client_t *c, const uint8_t *data, size_t len)
{
    std::lock_guard<std::mutex> lk(c->send_mutex);
    if (c->fd < 0) return -1;
    return sock_send_locked(c->fd, data, len);
}

/** Read exactly n bytes from fd, with per-recv select() guard (30 s dead-connection timeout).
 *  Returns 0 on success, -1 on error or timeout.
 *  Using select() rather than SO_RCVTIMEO prevents false timeouts mid-packet when
 *  the broker round-trip is high but data is still flowing. */
static int sock_recv(int fd, uint8_t *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = { 30, 0 }; // 30 s dead-socket guard
        int s = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (s <= 0) return -1;         // timeout or error → treat as dead
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

/** Read MQTT variable-length remaining length from fd. */
static int recv_varlen(int fd, uint32_t *out)
{
    uint32_t val = 0, mul = 1;
    for (int i = 0; i < 4; i++) {
        uint8_t b;
        if (sock_recv(fd, &b, 1) < 0) return -1;
        val += (uint32_t)(b & 0x7F) * mul;
        mul *= 128;
        if (!(b & 0x80)) { *out = val; return 0; }
    }
    return -1; // malformed
}

// ---------------------------------------------------------------------------
// MQTT packet builders
// ---------------------------------------------------------------------------

/** Send MQTT CONNECT. */
static int send_connect(mac_mqtt_client_t *c)
{
    uint8_t payload[640];
    int     plen = 0;

    // Client ID
    const char *cid = c->config.client_id;
    char auto_id[24];
    if (!cid || cid[0] == '\0') {
        snprintf(auto_id, sizeof(auto_id), "ferp-sim-%04X", (unsigned)(rand() & 0xFFFF));
        cid = auto_id;
    }
    plen += write_str(payload + plen, cid);

    // Connect flags
    uint8_t flags = 0;
    if (!c->config.disable_clean_session) flags |= 0x02; // clean session

    bool has_will = c->config.use_lwt;
    if (has_will) {
        flags |= 0x04;
        flags |= (uint8_t)((c->config.lwt.qos & 0x03) << 3);
        if (c->config.lwt.retain) flags |= 0x20;
        plen += write_str(payload + plen, c->config.lwt.topic);
        plen += write_str(payload + plen, c->config.lwt.message);
    }

    bool has_user = (c->config.username[0] != '\0');
    bool has_pass = (c->config.password[0] != '\0');
    if (has_user) { flags |= 0x80; plen += write_str(payload + plen, c->config.username); }
    if (has_pass) { flags |= 0x40; plen += write_str(payload + plen, c->config.password); }

    uint16_t ka = (c->config.keepalive > 0) ? c->config.keepalive : 60;

    // Variable header: protocol name (6 bytes) + level + flags + keepalive = 10 bytes
    uint8_t var_hdr[10] = {
        0x00, 0x04, 'M', 'Q', 'T', 'T', // Protocol Name "MQTT"
        0x04,                             // Protocol Level 3.1.1
        flags,
        (uint8_t)(ka >> 8), (uint8_t)(ka & 0xFF)
    };

    uint32_t remaining = 10 + (uint32_t)plen;
    uint8_t  fixed[5];
    fixed[0] = 0x10; // CONNECT
    int vlen = write_varlen(fixed + 1, remaining);

    size_t   total = 1 + (size_t)vlen + remaining;
    uint8_t *pkt   = (uint8_t *)malloc(total);
    if (!pkt) return -1;

    pkt[0] = fixed[0];
    memcpy(pkt + 1, fixed + 1, (size_t)vlen);
    memcpy(pkt + 1 + vlen, var_hdr, 10);
    memcpy(pkt + 1 + vlen + 10, payload, (size_t)plen);

    int ret;
    {
        std::lock_guard<std::mutex> lk(c->send_mutex);
        ret = sock_send_locked(c->fd, pkt, total);
    }
    free(pkt);
    return ret;
}

/** Send MQTT SUBSCRIBE. */
static int send_subscribe(mac_mqtt_client_t *c, const char *topic, pal_mqtt_qos_t qos)
{
    uint16_t msg_id = ++(c->next_msg_id);
    if (msg_id == 0) msg_id = ++(c->next_msg_id);

    size_t   tlen      = strlen(topic);
    uint32_t remaining = 2u + 2u + (uint32_t)tlen + 1u;

    uint8_t buf[640];
    int pos = 0;
    buf[pos++] = 0x82; // SUBSCRIBE
    pos += write_varlen(buf + pos, remaining);
    buf[pos++] = (uint8_t)(msg_id >> 8);
    buf[pos++] = (uint8_t)(msg_id & 0xFF);
    pos += write_str(buf + pos, topic);
    buf[pos++] = (uint8_t)(qos & 0x03);
    return sock_send(c, buf, (size_t)pos);
}

/** Send MQTT UNSUBSCRIBE. */
static int send_unsubscribe(mac_mqtt_client_t *c, const char *topic)
{
    uint16_t msg_id = ++(c->next_msg_id);
    if (msg_id == 0) msg_id = ++(c->next_msg_id);

    size_t   tlen      = strlen(topic);
    uint32_t remaining = 2u + 2u + (uint32_t)tlen;

    uint8_t buf[640];
    int pos = 0;
    buf[pos++] = 0xA2; // UNSUBSCRIBE
    pos += write_varlen(buf + pos, remaining);
    buf[pos++] = (uint8_t)(msg_id >> 8);
    buf[pos++] = (uint8_t)(msg_id & 0xFF);
    pos += write_str(buf + pos, topic);
    return sock_send(c, buf, (size_t)pos);
}

/** Send MQTT PUBLISH (QoS 0 or 1). */
static int send_publish(mac_mqtt_client_t *c, const char *topic,
                         const void *data, size_t data_len, pal_mqtt_qos_t qos, bool retain)
{
    size_t   tlen      = strlen(topic);
    uint32_t remaining = 2u + (uint32_t)tlen + (qos > 0 ? 2u : 0u) + (uint32_t)data_len;

    uint8_t fixed_hdr[5];
    fixed_hdr[0] = (uint8_t)(0x30 | ((qos & 0x03) << 1) | (retain ? 1 : 0));
    int vlen = write_varlen(fixed_hdr + 1, remaining);

    size_t   total = 1 + (size_t)vlen + remaining;
    uint8_t *pkt   = (uint8_t *)malloc(total);
    if (!pkt) return -1;

    int pos = 0;
    pkt[pos++] = fixed_hdr[0];
    memcpy(pkt + pos, fixed_hdr + 1, (size_t)vlen); pos += vlen;
    pos += write_str(pkt + pos, topic);

    if (qos > 0) {
        uint16_t mid = ++(c->next_msg_id);
        if (mid == 0) mid = ++(c->next_msg_id);
        pkt[pos++] = (uint8_t)(mid >> 8);
        pkt[pos++] = (uint8_t)(mid & 0xFF);
    }
    if (data_len > 0) memcpy(pkt + pos, data, data_len);

    int ret;
    {
        std::lock_guard<std::mutex> lk(c->send_mutex);
        ret = sock_send_locked(c->fd, pkt, total);
    }
    free(pkt);
    return ret;
}

/** Send PUBACK for a received QoS 1 PUBLISH. */
static int send_puback(mac_mqtt_client_t *c, uint16_t msg_id)
{
    uint8_t buf[4] = { 0x40, 0x02, (uint8_t)(msg_id >> 8), (uint8_t)(msg_id & 0xFF) };
    return sock_send(c, buf, 4);
}

/** Send PINGREQ. */
static int send_pingreq(mac_mqtt_client_t *c)
{
    uint8_t buf[2] = { 0xC0, 0x00 };
    return sock_send(c, buf, 2);
}

/** Send DISCONNECT. */
static int send_disconnect(mac_mqtt_client_t *c)
{
    uint8_t buf[2] = { 0xE0, 0x00 };
    return sock_send(c, buf, 2);
}

// ---------------------------------------------------------------------------
// Event helpers
// ---------------------------------------------------------------------------

static void fire_event(mac_mqtt_client_t *c, pal_mqtt_event_t type)
{
    if (!c->callback) return;
    pal_mqtt_event_data_t ev = {};
    ev.event_type = type;
    ev.client     = c;
    ev.user_data  = c->user_data;
    c->callback(&ev);
}

static void fire_data_event(mac_mqtt_client_t *c,
                             const char *topic, size_t topic_len,
                             const char *data,  size_t data_len,
                             int qos, bool retain, bool dup)
{
    if (!c->callback) return;
    pal_mqtt_event_data_t ev = {};
    ev.event_type                       = PAL_MQTT_EVENT_DATA;
    ev.client                           = c;
    ev.user_data                        = c->user_data;
    ev.data.message.topic               = topic;
    ev.data.message.topic_len           = topic_len;
    ev.data.message.data                = data;
    ev.data.message.data_len            = data_len;
    ev.data.message.total_data_len      = data_len;
    ev.data.message.current_data_offset = 0;
    ev.data.message.qos                 = (pal_mqtt_qos_t)qos;
    ev.data.message.retain              = retain;
    ev.data.message.dup                 = dup;
    c->callback(&ev);
}

static void fire_error_event(mac_mqtt_client_t *c, int32_t code)
{
    if (!c->callback) return;
    pal_mqtt_event_data_t ev = {};
    ev.event_type      = PAL_MQTT_EVENT_ERROR;
    ev.client          = c;
    ev.user_data       = c->user_data;
    ev.data.error_code = code;
    c->callback(&ev);
}

// ---------------------------------------------------------------------------
// URI / host parsing
// ---------------------------------------------------------------------------

/** Parse broker_uri "mqtt://host:port" or bare "host" into c->host and c->port. */
static void parse_uri(mac_mqtt_client_t *c)
{
    const char *uri         = c->config.broker_uri;
    const char *host_start  = uri;
    uint16_t    default_port = 1883;

    if (strncmp(uri, "mqtts://", 8) == 0) { host_start = uri + 8; default_port = 8883; }
    else if (strncmp(uri, "mqtt://", 7) == 0) { host_start = uri + 7; }

    // Separate host from optional port
    const char *colon = strrchr(host_start, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - host_start);
        if (hlen >= sizeof(c->host)) hlen = sizeof(c->host) - 1;
        memcpy(c->host, host_start, hlen);
        c->host[hlen] = '\0';
        c->port = (uint16_t)atoi(colon + 1);
    } else {
        strncpy(c->host, host_start, sizeof(c->host) - 1);
        c->host[sizeof(c->host) - 1] = '\0';
        c->port = default_port;
    }

    // config.port overrides when no port was embedded in the URI
    if (c->config.port > 0 && !colon) {
        c->port = c->config.port;
    }

    if (c->port == 0) c->port = 1883;
}

// ---------------------------------------------------------------------------
// TCP connection
// ---------------------------------------------------------------------------

static int tcp_connect(mac_mqtt_client_t *c)
{
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", c->port);

    int err = getaddrinfo(c->host, port_str, &hints, &res);
    if (err != 0 || !res) {
        LOG_MSG_WARNING(true, "MQTT getaddrinfo(%s:%u) failed: %s",
                        c->host, c->port, gai_strerror(err));
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    // NOTE: Do NOT set SO_RCVTIMEO here. The receive loop uses select() with a
    // 1-second poll interval for keepalive, so recv() is only called when select()
    // has already confirmed data is available. A recv timeout causes mid-packet
    // reads to fail on slow/remote brokers (e.g. RTT > 3 s for a PINGRESP body
    // fragment), which erroneously triggers a disconnect + reconnect loop.

    uint32_t timeout_ms = c->config.network_timeout_ms > 0
                          ? c->config.network_timeout_ms : 10000;
    struct timeval stv = {
        (time_t)(timeout_ms / 1000),
        (suseconds_t)((timeout_ms % 1000) * 1000)
    };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        LOG_MSG_WARNING(true, "MQTT connect(%s:%u) failed: %s",
                        c->host, c->port, strerror(errno));
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    c->fd = fd;
    LOG_MSG_INFO(true, "MQTT TCP connected to %s:%u", c->host, c->port);
    return 0;
}

// ---------------------------------------------------------------------------
// Packet handler
// ---------------------------------------------------------------------------

static void handle_packet(mac_mqtt_client_t *c, uint8_t type_byte,
                           const uint8_t *pkt, uint32_t len)
{
    uint8_t ptype = (type_byte >> 4) & 0x0F;

    switch (ptype) {
        case 2: { // CONNACK
            if (len < 2) break;
            uint8_t rc = pkt[1];
            if (rc == 0) {
                c->is_connected = true;
                LOG_MSG_INFO(true, "MQTT CONNACK: connected");
                fire_event(c, PAL_MQTT_EVENT_CONNECTED);
            } else {
                LOG_MSG_WARNING(true, "MQTT CONNACK refused, rc=%u", rc);
                fire_error_event(c, (int32_t)rc);
            }
            break;
        }

        case 3: { // PUBLISH
            if (len < 2) break;
            int  qos    = (type_byte >> 1) & 0x03;
            bool retain = (type_byte & 0x01) != 0;
            bool dup    = (type_byte & 0x08) != 0;

            uint16_t tlen = ((uint16_t)pkt[0] << 8) | pkt[1];
            if ((uint32_t)(2 + tlen) > len) break;

            char *topic_buf = (char *)malloc(tlen + 1);
            if (!topic_buf) break;
            memcpy(topic_buf, pkt + 2, tlen);
            topic_buf[tlen] = '\0';

            uint32_t pos = 2 + tlen;
            uint16_t msg_id = 0;
            if (qos > 0) {
                if (len < pos + 2) { free(topic_buf); break; }
                msg_id = ((uint16_t)pkt[pos] << 8) | pkt[pos + 1];
                pos += 2;
            }

            size_t dlen     = (pos < len) ? (size_t)(len - pos) : 0;
            char  *data_buf = (char *)malloc(dlen + 1);
            if (data_buf) {
                if (dlen > 0) memcpy(data_buf, pkt + pos, dlen);
                data_buf[dlen] = '\0';
                fire_data_event(c, topic_buf, tlen, data_buf, dlen, qos, retain, dup);
                free(data_buf);
            }
            free(topic_buf);

            if (qos == 1 && msg_id > 0) send_puback(c, msg_id);
            break;
        }

        case 4:  // PUBACK — ACK for our QoS 1 PUBLISH
            fire_event(c, PAL_MQTT_EVENT_PUBLISHED);
            break;

        case 9:  // SUBACK
            fire_event(c, PAL_MQTT_EVENT_SUBSCRIBED);
            break;

        case 11: // UNSUBACK
            fire_event(c, PAL_MQTT_EVENT_UNSUBSCRIBED);
            break;

        case 13: // PINGRESP
            break;

        default:
            LOG_MSG_WARNING(true, "MQTT unexpected packet type %u", ptype);
            break;
    }
}

// ---------------------------------------------------------------------------
// Connection / receive thread
// ---------------------------------------------------------------------------

static void connection_thread_fn(mac_mqtt_client_t *c)
{
    // Prevent SIGPIPE from killing the process when writing to a closed socket
    signal(SIGPIPE, SIG_IGN);

    while (!c->should_stop) {
        // ── TCP connect ──────────────────────────────────────────────────────
        if (tcp_connect(c) < 0) {
            uint32_t delay_ms = (c->config.reconnect_timeout_ms > 0)
                                ? c->config.reconnect_timeout_ms : 5000;
            for (uint32_t t = 0; t < delay_ms && !c->should_stop; t += 200)
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        // ── MQTT CONNECT ─────────────────────────────────────────────────────
        if (send_connect(c) < 0) {
            std::lock_guard<std::mutex> lk(c->send_mutex);
            close(c->fd); c->fd = -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            continue;
        }

        // ── Receive loop ─────────────────────────────────────────────────────
        uint32_t keepalive_ms = ((c->config.keepalive > 0 ? c->config.keepalive : 60) * 1000U) / 2U;
        auto last_ping = std::chrono::steady_clock::now();
        bool error = false;

        while (!c->should_stop && !error) {
            // Keepalive
            auto now     = std::chrono::steady_clock::now();
            auto elapsed = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - last_ping).count();
            if (elapsed >= keepalive_ms) {
                if (send_pingreq(c) < 0) { error = true; break; }
                last_ping = std::chrono::steady_clock::now();
            }

            // Poll for incoming data (1-second granularity for keepalive)
            fd_set rfds;
            FD_ZERO(&rfds);
            int cur_fd = c->fd;
            if (cur_fd < 0) { error = true; break; }
            FD_SET(cur_fd, &rfds);
            struct timeval tv = { 1, 0 };
            int sel = select(cur_fd + 1, &rfds, nullptr, nullptr, &tv);
            if (c->should_stop) break;
            if (sel < 0)  { error = true; break; }
            if (sel == 0) continue; // timeout — loop for keepalive check

            // Read fixed header
            uint8_t type_byte;
            if (sock_recv(cur_fd, &type_byte, 1) < 0) { error = true; break; }

            uint32_t remaining = 0;
            if (recv_varlen(cur_fd, &remaining) < 0) { error = true; break; }

            uint8_t *pkt = nullptr;
            if (remaining > 0) {
                pkt = (uint8_t *)malloc(remaining);
                if (!pkt || sock_recv(cur_fd, pkt, remaining) < 0) {
                    free(pkt); error = true; break;
                }
            }
            handle_packet(c, type_byte, pkt, remaining);
            free(pkt);
        }

        // ── Cleanup after disconnect ─────────────────────────────────────────
        bool was_connected = c->is_connected.exchange(false);
        {
            std::lock_guard<std::mutex> lk(c->send_mutex);
            if (c->fd >= 0) { close(c->fd); c->fd = -1; }
        }

        if (was_connected && !c->should_stop) {
            LOG_MSG_WARNING(true, "MQTT disconnected — will reconnect");
            fire_event(c, PAL_MQTT_EVENT_DISCONNECTED);
        }

        if (!c->should_stop) {
            uint32_t delay_ms = (c->config.reconnect_timeout_ms > 0)
                                ? c->config.reconnect_timeout_ms : 5000;
            for (uint32_t t = 0; t < delay_ms && !c->should_stop; t += 200)
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    LOG_MSG_INFO(true, "MQTT connection thread exit");
}

// ---------------------------------------------------------------------------
// PAL API
// ---------------------------------------------------------------------------

extern "C" void pal_mqtt_get_default_config(pal_mqtt_config_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->keepalive            = 60;
    cfg->network_timeout_ms   = 10000;
    cfg->reconnect_timeout_ms = 5000;
    cfg->buffer_size          = 1024;
    cfg->transport            = PAL_MQTT_TRANSPORT_OVER_TCP;
}

pal_mqtt_client_handle_t pal_mqtt_client_init(const pal_mqtt_config_t *config,
                                               pal_mqtt_event_callback_t event_callback,
                                               void *user_data)
{
    if (!config || !event_callback) return nullptr;

    auto *c = new mac_mqtt_client_t{};
    c->config       = *config;
    c->callback     = event_callback;
    c->user_data    = user_data;
    c->fd           = -1;
    c->is_connected = false;
    c->started      = false;
    c->should_stop  = false;
    c->next_msg_id  = 0;

    parse_uri(c);

    LOG_MSG_INFO(true, "MQTT client init: %s:%u  client_id='%s'",
                 c->host, c->port, c->config.client_id);
    return c;
}

int32_t pal_mqtt_client_start(pal_mqtt_client_handle_t client)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    if (!c) return -1;
    if (c->started.exchange(true)) return 0; // already started
    c->should_stop = false;
    c->conn_thread = std::thread(connection_thread_fn, c);
    LOG_MSG_INFO(true, "MQTT client started");
    return 0;
}

int32_t pal_mqtt_client_stop(pal_mqtt_client_handle_t client)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    if (!c) return -1;
    c->should_stop = true;
    if (c->is_connected.exchange(false)) {
        send_disconnect(c);
    }
    {
        std::lock_guard<std::mutex> lk(c->send_mutex);
        if (c->fd >= 0) { shutdown(c->fd, SHUT_RDWR); close(c->fd); c->fd = -1; }
    }
    return 0;
}

int32_t pal_mqtt_client_destroy(pal_mqtt_client_handle_t client)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    if (!c) return -1;
    pal_mqtt_client_stop(client);
    if (c->conn_thread.joinable()) c->conn_thread.join();
    delete c;
    return 0;
}

int32_t pal_mqtt_client_reconnect(pal_mqtt_client_handle_t client)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    if (!c) return -1;
    pal_mqtt_client_stop(client);
    c->started     = false;
    c->should_stop = false;
    return pal_mqtt_client_start(client);
}

int32_t pal_mqtt_client_subscribe(pal_mqtt_client_handle_t client,
                                   const char *topic, pal_mqtt_qos_t qos)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    if (!c || !topic) return -1;
    if (!c->is_connected) {
        LOG_MSG_WARNING(true, "MQTT subscribe('%s') called while not connected", topic);
        return -1;
    }
    return send_subscribe(c, topic, qos);
}

int32_t pal_mqtt_client_unsubscribe(pal_mqtt_client_handle_t client, const char *topic)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    if (!c || !topic) return -1;
    if (!c->is_connected) return -1;
    return send_unsubscribe(c, topic);
}

int32_t pal_mqtt_client_publish(pal_mqtt_client_handle_t client,
                                 const char *topic, const char *data,
                                 size_t len, pal_mqtt_qos_t qos, bool retain)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    if (!c || !topic || !data) return -1;
    if (!c->is_connected) return -1;

    size_t dlen = (len > 0) ? len : strlen(data);
    int    ret  = send_publish(c, topic, data, dlen, qos, retain);
    if (ret == 0 && qos == PAL_MQTT_QOS_0) {
        // QoS 0: fire PUBLISHED immediately (no PUBACK)
        fire_event(c, PAL_MQTT_EVENT_PUBLISHED);
    }
    return ret;
}

bool pal_mqtt_client_is_connected(pal_mqtt_client_handle_t client)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    return c && c->is_connected.load();
}

int32_t pal_mqtt_client_get_state(pal_mqtt_client_handle_t client)
{
    auto *c = static_cast<mac_mqtt_client_t *>(client);
    if (!c) return -1;
    return c->is_connected ? 1 : 0;
}
