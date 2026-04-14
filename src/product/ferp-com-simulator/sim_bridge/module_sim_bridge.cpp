/**
 * @file module_sim_bridge.cpp
 * @brief Simulator bridge — serialises HSYS messages to JSON and sends them
 *        to the Python UI over a TCP socket.
 *
 * Currently subscribed messages (grows with each sprint):
 *   MSG_ID_TICK_1000MS   → heartbeat counter
 *   MSG_ID_SENSOR_DATA   → demo sensor value  (removed when demo modules retire)
 *
 * When you add a new message type, add:
 *   1. subscribe(MsgXxx::ID) in init()
 *   2. A case in on_msg_received() that calls _send_json()
 */

#include "module_sim_bridge.h"
#include "app_msg_ids.h"
#include "hsys_mutex.h"
#include "hsys_pool.h"
#include "hsys_msg.h"
#include "pal_time.h"

#include <stdio.h>
#include <string.h>
#include <thread>

// POSIX sockets (macOS / Linux)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// ─── singleton ───────────────────────────────────────────────────────────────

static ModuleSimBridge s_instance;
ModuleSimBridge *ModuleSimBridge::instance() { return &s_instance; }

// ─── TCP server setup ─────────────────────────────────────────────────────────

void ModuleSimBridge::start_server(uint16_t port)
{
    _port  = port;
    _mutex = hsys_mutex_create();

    _server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_server_fd < 0) {
        log_error("socket() failed: %s", strerror(errno));
        return;
    }

    int opt = 1;
    ::setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(_server_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("bind() failed on port %u: %s", port, strerror(errno));
        ::close(_server_fd);
        _server_fd = -1;
        return;
    }

    ::listen(_server_fd, 1);
    log("TCP UI server listening on port %u", port);

    // Accept loop runs in a detached background thread — it blocks on accept()
    std::thread([this]{ _accept_loop(); }).detach();
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void ModuleSimBridge::init()
{
    // Subscribe to every message that the Python UI wants to visualise.
    // Add entries here as each sprint adds new message types.
    subscribe(MSG_ID_TICK_1000MS);
    // subscribe(MSG_ID_SENSOR_DATA);  // uncomment once demo module is live

    log("init  (UI port %u)", _port);
}

// ─── Message handler ─────────────────────────────────────────────────────────

void ModuleSimBridge::on_msg_received(const hsys_msg_t &msg)
{
    char data[256];

    switch (msg.msg_id)
    {
        case MSG_ID_TICK_1000MS:
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_TICK_1000MS", data);
            ++_tick_count;
            if (_tick_count % POOL_REPORT_INTERVAL_TICKS == 0) {
                _send_pool_status();
            }
            break;

        // ── Add cases here as each sprint adds message types ──────────────
        //
        // case MSG_ID_WIFI_EVENT: {
        //     auto *m = hsys_msg_cast<MsgWifiEvent>(msg);
        //     snprintf(data, sizeof(data),
        //              "{\"event\":\"%s\",\"rssi\":%d}",
        //              wifi_event_to_str(m->event_id), m->rssi);
        //     _send_json("MSG_WIFI_EVENT", data);
        //     break;
        // }
        //
        // case MSG_ID_NOZZLE_STATE: {
        //     auto *m = hsys_msg_cast<MsgNozzleState>(msg);
        //     snprintf(data, sizeof(data),
        //              "{\"idx\":%u,\"state\":\"%s\",\"ts\":%llu}",
        //              m->nozzle_idx,
        //              m->state == APP_PUMP_STARTED ? "PUMPING" : "IDLE",
        //              (unsigned long long)m->timestamp_ms);
        //     _send_json("MSG_NOZZLE_STATE", data);
        //     break;
        // }

        default:
            break;
    }
}

// ─── TCP helpers ─────────────────────────────────────────────────────────────

void ModuleSimBridge::_accept_loop()
{
    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t   len = sizeof(client_addr);
        int fd = ::accept(_server_fd, (sockaddr *)&client_addr, &len);
        if (fd < 0) {
            // Server socket closed — exit
            break;
        }

        char ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        log("UI client connected from %s", ip);

        // Store the client fd (lock because writer uses it too)
        hsys_mutex_lock(_mutex);
        if (_client_fd >= 0) ::close(_client_fd);
        _client_fd = fd;
        hsys_mutex_unlock(_mutex);

        // Spin up a reader thread for inbound commands; this call blocks until
        // the client disconnects, then loops back to accept().
        _read_loop(fd);

        log("UI client disconnected");
        hsys_mutex_lock(_mutex);
        _client_fd = -1;
        hsys_mutex_unlock(_mutex);
    }
}

void ModuleSimBridge::_read_loop(int client_fd)
{
    char line_buf[512];
    size_t pos = 0;
    char ch;

    while (::read(client_fd, &ch, 1) == 1)
    {
        if (ch == '\n')
        {
            line_buf[pos] = '\0';
            pos = 0;

            // Very minimal JSON parse — just look at "id" field
            // A proper JSON parser can be added later if needed
            if (strstr(line_buf, "\"SIM_BTN\"")) {
                log("UI cmd: %s", line_buf);
                // TODO sprint 5: parse btn name, publish MsgPrintBtnEvent /
                // MsgDefaultBtnEvent
            } else if (strstr(line_buf, "\"SIM_MQTT_INJECT\"")) {
                log("UI cmd: %s", line_buf);
                // TODO sprint 7: parse topic+payload, publish MsgMqttRxMessage
            } else if (strstr(line_buf, "\"SIM_OTA_TRIGGER\"")) {
                log("UI cmd: %s", line_buf);
                // TODO sprint 9: parse driver index, publish MsgOtaTrigger
            }
        }
        else if (pos < sizeof(line_buf) - 1)
        {
            line_buf[pos++] = ch;
        }
    }
}

void ModuleSimBridge::_send_json(const char *id, const char *data_json)
{
    if (_server_fd < 0) return;

    char line[512];
    int n = snprintf(line, sizeof(line) - 1,
                     "{\"id\":\"%s\",\"ts\":%llu,\"data\":%s}\n",
                     id,
                     (unsigned long long)pal_time_get_ms(),
                     data_json);
    if (n <= 0) return;

    hsys_mutex_lock(_mutex);
    int fd = _client_fd;
    if (fd >= 0) {
        ::write(fd, line, (size_t)n);
    }
    hsys_mutex_unlock(_mutex);
}

// ─── Pool status serialiser ───────────────────────────────────────────────────

void ModuleSimBridge::_send_pool_status()
{
    // Build the JSON inline into a stack buffer.
    // Format:
    //  {"classes":[{"idx":0,"block_size":4,"total":8,"free":8,"used":0}, ...],
    //   "hdr":{"total":32,"free":30,"used":2,"peak":2},
    //   "ts_ms":5000}

    char buf[768];
    int  pos = 0;
    int  rem = (int)sizeof(buf);

#define APPEND(...) do { int w = snprintf(buf+pos, (size_t)rem, __VA_ARGS__); \
                         if (w > 0) { pos += w; rem -= w; } } while(0)

    APPEND("{\"classes\":[");

    uint8_t idx = 0;
    hsys_pool_class_info_t info;
    bool first = true;

    while (hsys_pool_get_info(idx, &info) == HSYS_OK) {
        uint16_t used = info.total_count - info.free_count;
        if (!first) APPEND(",");
        first = false;
        APPEND("{\"idx\":%u,\"block_size\":%u,\"total\":%u,\"free\":%u,\"used\":%u}",
               (unsigned)idx,
               (unsigned)info.block_size,
               (unsigned)info.total_count,
               (unsigned)info.free_count,
               (unsigned)used);
        ++idx;
    }

    APPEND("],");

    hsys_msg_header_pool_info_t hdr;
    hsys_msg_get_header_pool_info(&hdr);

    APPEND("\"hdr\":{\"total\":%u,\"free\":%u,\"used\":%u,\"peak\":%u},",
           (unsigned)hdr.total_slots,
           (unsigned)hdr.free_slots,
           (unsigned)hdr.used_slots,
           (unsigned)hdr.peak_used_slots);

    APPEND("\"ts_ms\":%llu}", (unsigned long long)pal_time_get_ms());

#undef APPEND

    _send_json("SIM_POOL_STATUS", buf);
}
