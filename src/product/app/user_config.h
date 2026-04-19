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
#define HSYS_MAX_TASKS      12   ///< enough for all current + near-future tasks
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

// Legacy alias used in middleware LOG_MSG_* calls
#ifndef LOG_EN
#define LOG_EN               true
#endif
