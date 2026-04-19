/**
 * @file mac_driver.cpp
 * @brief macOS simulator hardware driver implementation.
 *
 * This is the ONLY file in the simulator that owns a TCP socket.
 * All other mac-pc PAL files send data through mac_driver_send_json() /
 * mac_driver_send_gpio(), and receive simulated hardware input via callbacks
 * that are triggered from the read loop here.
 *
 * Outbound (C++ → Python):
 *   mac_driver_send_json()  — generic JSON line
 *   mac_driver_send_gpio()  — GPIO output level change  (used by pal_mac_gpio.cpp)
 *
 * Inbound (Python → C++):
 *   "SIM_BTN"          → maps button name to GPIO pin, calls pal_gpio_sim_inject_input()
 *   "SIM_MQTT_INJECT"  → (TODO sprint 7) calls pal_mac_mqtt inject
 *   "SIM_OTA_TRIGGER"  → (TODO sprint 9)
 *
 * Button → pin mapping (matches board_2602_wrap.h aliases):
 *   "default"  → INPUT5 / GPIO_NUM_36
 *   "print1"   → INPUT1 / GPIO_NUM_34
 *   "print2"   → INPUT2 / GPIO_NUM_35
 *   "nozzle1"  → INPUT3 / GPIO_NUM_32
 *   "nozzle2"  → INPUT4 / GPIO_NUM_33
 */

#include "mac_driver.h"
#include "pal_time.h"
#include "pal_logger.h"

#include <stdio.h>
#include <string.h>
#include <thread>
#include <mutex>

// POSIX sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#define __TAG__      "MAC_DRV "
#define MAC_DRV_LOG  true

/* Avoid colliding with cmath log() — use MLOG / MLOGE as local shorthands */
#define MLOG(fmt, ...)  LOG_MSG_INFO( MAC_DRV_LOG, fmt, ##__VA_ARGS__)
#define MLOGE(fmt, ...) LOG_MSG_ERROR(MAC_DRV_LOG, fmt, ##__VA_ARGS__)

/* ── Forward declaration — implemented in pal_mac_gpio.cpp ─────────────────── */
extern "C" void pal_gpio_sim_inject_input(int pin, int level);

/* ── Internal state ──────────────────────────────────────────────────────────*/
static int          s_server_fd = -1;
static int          s_client_fd = -1;
static std::mutex   s_mutex;

/* ── Button → GPIO pin map ───────────────────────────────────────────────────
 * Mirrors board_2602_wrap.h aliases.  PIN numbers come from board_inf.h.
 */
static const struct { const char *btn; int pin; } k_btn_map[] = {
    { "default",  36 },   /* INPUT5 / DEFAULT_BUTTON_GPIO_PIN */
    { "print1",   34 },   /* INPUT1 / PRINT1_BUTTON_GPIO_PIN  */
    { "print2",   35 },   /* INPUT2 / PRINT2_BUTTON_GPIO_PIN  */
    { "nozzle1",  32 },   /* INPUT3 / NOZZLE1_GPIO_PIN        */
    { "nozzle2",  33 },   /* INPUT4 / NOZZLE2_GPIO_PIN        */
};
static constexpr int k_btn_map_count = (int)(sizeof(k_btn_map) / sizeof(k_btn_map[0]));

/* ── Read loop — handles inbound commands from Python UI ─────────────────── */
static void _read_loop(int client_fd)
{
    char   buf[512];
    size_t pos = 0;
    char   ch;

    while (::read(client_fd, &ch, 1) == 1)
    {
        if (ch == '\n')
        {
            buf[pos] = '\0';
            pos = 0;

            /* ── SIM_BTN ─────────────────────────────────────────────────── */
            if (strstr(buf, "\"SIM_BTN\""))
            {
                MLOG("UI cmd: %s", buf);

                /* Parse "btn":"<name>" */
                const char *btn_key = strstr(buf, "\"btn\"");
                if (btn_key)
                {
                    const char *q1 = strchr(btn_key + 5, '"');
                    if (q1)
                    {
                        ++q1;
                        const char *q2 = strchr(q1, '"');
                        if (q2 && (q2 - q1) < 32)
                        {
                            char btn_name[32];
                            memcpy(btn_name, q1, (size_t)(q2 - q1));
                            btn_name[q2 - q1] = '\0';

                            /* "action":"press" → level 1 · "action":"release" → level 0 */
                            int level = strstr(buf, "\"press\"") ? 1 : 0;

                            /* Look up GPIO pin and inject */
                            for (int i = 0; i < k_btn_map_count; ++i)
                            {
                                if (strcmp(k_btn_map[i].btn, btn_name) == 0)
                                {
                                    pal_gpio_sim_inject_input(k_btn_map[i].pin, level);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            /* ── SIM_MQTT_INJECT ─────────────────────────────────────────── */
            else if (strstr(buf, "\"SIM_MQTT_INJECT\""))
            {
                MLOG("UI cmd: %s", buf);
                /* TODO sprint 7: parse topic+payload, call pal_mac_mqtt inject */
            }
            /* ── SIM_OTA_TRIGGER ─────────────────────────────────────────── */
            else if (strstr(buf, "\"SIM_OTA_TRIGGER\""))
            {
                MLOG("UI cmd: %s", buf);
                /* TODO sprint 9: parse driver index, call pal_mac_ota inject */
            }
        }
        else if (pos < sizeof(buf) - 1)
        {
            buf[pos++] = ch;
        }
    }
}

/* ── Accept loop — blocks forever waiting for a single client ───────────── */
static void _accept_loop()
{
    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t   len = sizeof(client_addr);
        int fd = ::accept(s_server_fd, (sockaddr *)&client_addr, &len);
        if (fd < 0) break;  /* server socket closed */

        char ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        MLOG("UI connected from %s", ip);

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_client_fd >= 0) ::close(s_client_fd);
            s_client_fd = fd;
        }

        _read_loop(fd);   /* blocks until client disconnects */

        MLOG("UI disconnected");
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_client_fd = -1;
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void mac_driver_init(uint16_t port)
{
    s_server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0)
    {
        MLOGE("socket() failed: %s", strerror(errno));
        return;
    }

    int opt = 1;
    ::setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(s_server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        MLOGE("bind() failed on port %u: %s", port, strerror(errno));
        ::close(s_server_fd);
        s_server_fd = -1;
        return;
    }

    ::listen(s_server_fd, 1);
    MLOG("TCP UI server listening on port %u", port);

    std::thread(_accept_loop).detach();
}

void mac_driver_send_json(const char *id, const char *data_json)
{
    if (s_server_fd < 0) return;

    char line[512];
    int n = snprintf(line, sizeof(line) - 1,
                     "{\"id\":\"%s\",\"ts\":%llu,\"data\":%s}\n",
                     id,
                     (unsigned long long)pal_time_get_ms(),
                     data_json);
    if (n <= 0) return;

    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_client_fd >= 0)
    {
        ::write(s_client_fd, line, (size_t)n);
    }
}

void mac_driver_send_gpio(int pin, int level, const char *name)
{
    char data[128];
    snprintf(data, sizeof(data),
             "{\"pin\":%d,\"level\":%d,\"name\":\"%s\"}",
             pin, level, name ? name : "");
    mac_driver_send_json("SIM_GPIO_OUT", data);
}
