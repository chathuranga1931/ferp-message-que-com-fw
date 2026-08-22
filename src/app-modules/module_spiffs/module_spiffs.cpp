// module_spiffs.cpp
//
// ModuleSpiffs — mounts the SPIFFS filesystem via app_spiffs, then
// broadcasts MsgSpiffsReady so other modules know they can access files.
//
// Also handles MsgSpiffsCleanup: erases the SPIFFS partition then reboots.

#include "module_spiffs.h"
#include "app_spiffs.h"
#include "msg_spiffs_ready.h"
#include "msg_spiffs_cleanup.h"
#include "msg_system_reboot.h"
#include "pal_logger.h"

#define __TAG__          "SPIFFS_M"
#ifndef MOD_SPIFFS_LOG_EN
#define MOD_SPIFFS_LOG_EN true
#endif

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleSpiffs s_instance;

ModuleSpiffs *ModuleSpiffs::instance() { return &s_instance; }

// ── Configuration (set before init) ─────────────────────────────────────────────

void ModuleSpiffs::set_stale_files(const char *const *paths, uint8_t count)
{
    _stale_files = paths;
    _stale_count = count;
}

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

    // Reclaim space from the other product's stale files before anyone
    // (config loader, OTA) touches the filesystem.
    _purge_stale_files();
}

// ── Stale-file purge ───────────────────────────────────────────────────────────

void ModuleSpiffs::_purge_stale_files()
{
    if (!_stale_files || _stale_count == 0) return;

    app_spiffs_info_t before = {};
    (void)app_spiffs_get_info(&before);

    uint8_t deleted = 0;
    for (uint8_t i = 0; i < _stale_count; i++) {
        const char *path = _stale_files[i];
        if (!path) continue;

        bool exists = false;
        if (app_spiffs_file_exists(path, &exists, 5000) != APP_SPIFFS_OK || !exists)
            continue;

        int32_t rc = app_spiffs_delete_file(path, 5000);
        if (rc == APP_SPIFFS_OK) {
            deleted++;
            LOG_MSG_INFO(MOD_SPIFFS_LOG_EN, "purged stale file: %s", path);
        } else {
            LOG_MSG_ERROR(MOD_SPIFFS_LOG_EN,
                          "failed to purge %s (rc=%ld)", path, (long)rc);
        }
    }

    if (deleted == 0) {
        LOG_MSG_INFO(MOD_SPIFFS_LOG_EN, "stale-file purge: nothing to remove");
        return;
    }

    app_spiffs_info_t after = {};
    (void)app_spiffs_get_info(&after);
    LOG_MSG_INFO(MOD_SPIFFS_LOG_EN,
                 "stale-file purge: removed %u file(s), free %zu -> %zu bytes",
                 (unsigned)deleted, before.free_bytes, after.free_bytes);
}

void ModuleSpiffs::post_init()
{
    subscribe(MsgSpiffsCleanup::ID);

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

// ── Message handler ───────────────────────────────────────────────────────────

void ModuleSpiffs::on_msg_received(const hsys_msg_t &msg)
{
    if (msg.msg_id == MsgSpiffsCleanup::ID) {
        _on_spiffs_cleanup();
    }
}

// ── Cleanup handler ───────────────────────────────────────────────────────────

void ModuleSpiffs::_on_spiffs_cleanup()
{
    LOG_MSG_WARNING(MOD_SPIFFS_LOG_EN,
                    "MsgSpiffsCleanup received — erasing SPIFFS partition");

    int32_t rc = app_spiffs_format();
    if (rc != APP_SPIFFS_OK) {
        LOG_MSG_ERROR(MOD_SPIFFS_LOG_EN,
                      "cleanup: format failed (rc=%ld) — aborting", (long)rc);
        return;   // do NOT reboot; leave device running so the issue can be diagnosed
    }

    LOG_MSG_INFO(MOD_SPIFFS_LOG_EN, "cleanup: SPIFFS formatted — rebooting");

    hsys_msg_t *reboot = MsgSystemReboot::create(id());
    if (reboot) publish(reboot);
}
