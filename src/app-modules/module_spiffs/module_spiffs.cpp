// module_spiffs.cpp
//
// ModuleSpiffs — mounts the SPIFFS filesystem via app_spiffs, then
// broadcasts MsgSpiffsReady so other modules know they can access files.
//
// Also handles MsgSpiffsCleanup: erases the SPIFFS partition then reboots,
// and MsgSpiffsCleanDt: deletes the esp32/ + esp07/ DispTap image folders
// (the images now live on the SD card) and garbage-collects SPIFFS.

#include "module_spiffs.h"
#include "app_spiffs.h"
#include "msg_spiffs_ready.h"
#include "msg_spiffs_cleanup.h"
#include "app_msg_ids.h"
#ifdef APP_HAS_SPIFFS_CLEAN_DT
#include "msg_spiffs_clean_dt.h"
#include "msg_spiffs_clean_dt_result.h"
#endif
#include "msg_system_reboot.h"
#include "pal_logger.h"
#include <string.h>

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
#ifdef APP_HAS_SPIFFS_CLEAN_DT
    subscribe(MsgSpiffsCleanDt::ID);
#endif

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
#ifdef APP_HAS_SPIFFS_CLEAN_DT
    else if (msg.msg_id == MsgSpiffsCleanDt::ID) {
        _on_clean_dt(msg);
    }
#endif
}

// ── DispTap image folder cleanup (products with APP_HAS_SPIFFS_CLEAN_DT) ──────
#ifdef APP_HAS_SPIFFS_CLEAN_DT

namespace {

const char *const k_dt_dirs[] = { "esp32/", "esp07/" };

// Collected under the SPIFFS mutex (list callback), deleted afterwards.
struct DtFileList {
    char     names[16][48];
    uint8_t  count;
    uint32_t bytes;
};
DtFileList s_dt_list;   // static: keeps 800 B off the storage_task stack

void _collect_dt_file(const char *name, size_t size, void *ctx)
{
    DtFileList *l = static_cast<DtFileList *>(ctx);
    if (l->count >= sizeof(l->names) / sizeof(l->names[0])) return;
    for (const char *dir : k_dt_dirs) {
        if (strncmp(name, dir, strlen(dir)) == 0) {
            strncpy(l->names[l->count], name, sizeof(l->names[0]) - 1);
            l->names[l->count][sizeof(l->names[0]) - 1] = '\0';
            l->bytes += (uint32_t)size;
            l->count++;
            return;
        }
    }
}

}  // namespace

void ModuleSpiffs::_on_clean_dt(const hsys_msg_t &msg)
{
    MsgSpiffsCleanDtResult::Payload r{};
    app_spiffs_info_t info = {};
    (void)app_spiffs_get_info(&info);
    r.total       = (uint32_t)info.total_bytes;
    r.used_before = (uint32_t)info.used_bytes;

    LOG_MSG_WARNING(MOD_SPIFFS_LOG_EN, "MsgSpiffsCleanDt — deleting esp32/ and esp07/ (used %u of %u)",
                    (unsigned)r.used_before, (unsigned)r.total);

    // List then delete, in rounds (the list holds 16 names at a time)
    for (int round = 0; round < 4; round++) {
        memset(&s_dt_list, 0, sizeof(s_dt_list));
        if (app_spiffs_list_files(_collect_dt_file, &s_dt_list, 5000) != APP_SPIFFS_OK) {
            r.result = APP_SPIFFS_ERR_IO;
            break;
        }
        if (s_dt_list.count == 0) break;

        uint8_t deleted_now = 0;
        for (uint8_t i = 0; i < s_dt_list.count; i++) {
            int32_t rc = app_spiffs_delete_file(s_dt_list.names[i], 5000);
            if (rc == APP_SPIFFS_OK) {
                deleted_now++;
                LOG_MSG_INFO(MOD_SPIFFS_LOG_EN, "deleted %s", s_dt_list.names[i]);
            } else {
                r.result = rc;
                LOG_MSG_ERROR(MOD_SPIFFS_LOG_EN, "delete %s failed (%ld)", s_dt_list.names[i], (long)rc);
            }
        }
        r.files += deleted_now;
        r.bytes += s_dt_list.bytes;
        if (deleted_now == 0) break;      // nothing removable left
    }

    // Erase the blocks the deleted files occupied so the space is writable
    // straight away (each pass erases a bounded number of blocks).
    r.gc = 0;
    if (r.bytes > 0) {
        r.gc = APP_SPIFFS_ERR_IO;
        for (int pass = 0; pass < 10 && r.gc != APP_SPIFFS_OK; pass++) {
            r.gc = app_spiffs_gc(r.bytes, 5000);
        }
    }

    (void)app_spiffs_get_info(&info);
    r.used_after = (uint32_t)info.used_bytes;
    LOG_MSG_INFO(MOD_SPIFFS_LOG_EN, "clean_dt: %u file(s), %u B deleted; used %u -> %u of %u; gc=%ld",
                 (unsigned)r.files, (unsigned)r.bytes, (unsigned)r.used_before,
                 (unsigned)r.used_after, (unsigned)r.total, (long)r.gc);

    hsys_msg_t *resp = MsgSpiffsCleanDtResult::create(id(), r);
    if (!resp) return;
    if (msg.sender_id != (hsys_module_id_t)0) send(resp, msg.sender_id);
    else                                      publish(resp);
}
#endif  // APP_HAS_SPIFFS_CLEAN_DT

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
