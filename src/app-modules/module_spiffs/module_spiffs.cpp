// module_spiffs.cpp
//
// ModuleSpiffs — mounts the SPIFFS filesystem via app_spiffs, then
// broadcasts MsgSpiffsReady so other modules know they can access files.

#include "module_spiffs.h"
#include "app_spiffs.h"
#include "msg_spiffs_ready.h"
#include "pal_logger.h"

#define __TAG__          "SPIFFS_M"
#ifndef MOD_SPIFFS_LOG_EN
#define MOD_SPIFFS_LOG_EN true
#endif

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleSpiffs s_instance;

ModuleSpiffs *ModuleSpiffs::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleSpiffs::pre_init()
{
    LOG_MSG_INFO(MOD_SPIFFS_LOG_EN, "mounting SPIFFS…");

    int32_t rc = app_spiffs_init();
    if (rc != APP_SPIFFS_OK) {
        LOG_MSG_ERROR(MOD_SPIFFS_LOG_EN, "app_spiffs_init failed (%ld)", (long)rc);
        _mounted = false;
        return;
    }

    _mounted = true;
    LOG_MSG_INFO(MOD_SPIFFS_LOG_EN, "SPIFFS mounted OK");
}

void ModuleSpiffs::post_init()
{
    if (!_mounted) {
        LOG_MSG_ERROR(MOD_SPIFFS_LOG_EN, "skipping MsgSpiffsReady — mount failed");
        return;
    }

    LOG_MSG_INFO(MOD_SPIFFS_LOG_EN, "publishing MsgSpiffsReady");

    hsys_msg_t *msg = MsgSpiffsReady::create(id());
    if (msg) {
        publish(msg);
    } else {
        LOG_MSG_ERROR(MOD_SPIFFS_LOG_EN, "failed to create MsgSpiffsReady");
    }
}
