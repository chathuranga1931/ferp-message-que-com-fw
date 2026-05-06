// user_config.h
//
// Per-file log enable flags.
//
// This file is force-included first in every translation unit via the
// CMake -include flag, so these definitions are available before any
// other #include.
//
// Set a flag to `false` to silence that file's log output at compile time
// (the compiler will dead-strip the LOG_MSG_* calls entirely).

// ─────────────────────────────────────────────────────────────────────────────
// Framework tunables (override defaults from hsys_fw_config.h)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef HSYS_MAX_TASKS
#define HSYS_MAX_TASKS      16   ///< wifi_task + sd_task pushed count to 14; 16 gives headroom
#endif

#ifndef HSYS_MAX_MODULES
#define HSYS_MAX_MODULES    24   ///< 17 shared + OtaModule + ModuleSimBridge + ModuleWebServer (sim) + headroom
#endif

#ifndef HSYS_MAX_MSG_IDS
#define HSYS_MAX_MSG_IDS    1024 ///< Sizes s_id_map[] — 1 byte per slot = 1 KB total.
                                 ///< Highest assigned ID is 0x030C (780); 1024 gives headroom
                                 ///< for the full 0x0300 config domain and 0x00xx domains.
#endif

#ifndef HSYS_MAX_REGISTERED_MSGS
#define HSYS_MAX_REGISTERED_MSGS  64  ///< Sizes s_sub_table[] — 18 bytes per slot = 1.1 KB.
                                      ///< Currently 58 messages registered; 64 gives headroom.
#endif

#ifndef HSYS_POOL_MAX_BYTES
#define HSYS_POOL_MAX_BYTES 20480 ///< actual pool table needs 17440 B; 20480 adds 3 KB headroom.
                                  ///< Was default 32768, wasting 12 KB of BSS.
#endif
//
// Log enable flags — one per .cpp file that uses LOG_MSG_*
// ─────────────────────────────────────────────────────────────────────────────

// Framework
#define HSYS_MSG_LOG_EN     true    // hsys_msg.cpp        — message bus
#define HSYS_MOD_LOG_EN     true    // hsys_module.cpp     — module registry / lifecycle
#define HSYS_TSK_LOG_EN     true    // hsys_task_mgr.cpp   — task manager

// App-messages
#define MSG_SENS_LOG_EN     true    // msg_sensor_data.cpp
#define MSG_TICK_LOG_EN     true    // msg_tick_1000ms.cpp
#define MSG_CFGR_LOG_EN     true    // msg_config_ready.cpp
#define MSG_CFGS_LOG_EN     true    // msg_config_set.cpp
#define MSG_CFGG_LOG_EN     true    // msg_config_get.cpp

// App-modules
#define MOD_A_LOG_EN        true    // module_a.cpp
#define MOD_B_LOG_EN        true    // module_b.cpp
#define MOD_TICK_LOG_EN     true    // ticker.cpp
#define MOD_SYSMON_LOG_EN   true    // module_sysmon.cpp

// Simulator product
#define SIM_MAIN_LOG_EN     true    // main.cpp
#define SIM_INIT_LOG_EN     true    // sim_init.cpp
#define SIM_BRDG_LOG_EN     true    // module_sim_bridge.cpp
#define WEB_SRV_LOG_EN      true    // ModuleWebServer.cpp

// Middleware
#define HSYS_CFG_LOG_EN     true    // hsys_config.cpp

// ─────────────────────────────────────────────────────────────────────────────
// Error code base values — required by hsys_config.h
// ─────────────────────────────────────────────────────────────────────────────
#ifndef ERROR_OK
#define ERROR_OK             0
#endif

#ifndef ERR_CONFIG_OFFSET
#define ERR_CONFIG_OFFSET   -100
#endif

#ifndef ERR_DEV_INFO_OFFSET
#define ERR_DEV_INFO_OFFSET  -110
#endif

// Legacy alias used in middleware LOG_MSG_* calls
#ifndef LOG_EN
#define LOG_EN               true
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Logger UART configuration (used by pal_esp_idf_logger_uart.cpp)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef LOG_ENABLED
#define LOG_ENABLED          1       ///< Enable PAL UART logger (0 = disable)
#endif

#ifndef LOG_UART_BAUDRATE
#define LOG_UART_BAUDRATE    115200  ///< Baud rate for UART0 log output
#endif

#define BOARD_2602      1
#define FERP_COM_2602   1
#define DISTAP_ESP32
