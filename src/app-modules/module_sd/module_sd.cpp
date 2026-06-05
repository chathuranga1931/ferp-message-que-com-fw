// module_sd.cpp
//
// ModuleSD — mounts SD card via app_sd, then broadcasts:
//   MsgSdReady   — signal to other modules that the card is usable
//   MsgSdStatus  — detailed card info (type, total size, free space)
//
// Also subscribes to MsgSdCleanup: on receipt, wipes the entire SD card
// (all files and directories) then triggers a system reboot.

#include "module_sd.h"
#include "msg_sd_ready.h"
#include "msg_sd_status.h"
#include "msg_sd_cleanup.h"
#include "msg_system_reboot.h"
#include "pal_logger.h"
#include "pal_sd.h"
#include "app_hw_config.h"
#include <string.h>

#define __TAG__          "MOD_SD  "
#ifndef MOD_SD_LOG_EN
#define MOD_SD_LOG_EN true
#endif

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleSD s_instance;
ModuleSD *ModuleSD::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleSD::init()
{
    subscribe(MsgSdCleanup::ID);
    LOG_MSG_INFO(MOD_SD_LOG_EN, "init — subscribed to MsgSdCleanup");
}

void ModuleSD::pre_init()
{
    LOG_MSG_INFO(MOD_SD_LOG_EN, "mounting SD card…");

    pal_sd_config_t sd_cfg{};
    sd_cfg.mosi_pin = APP_HW_SD_MOSI;
    sd_cfg.miso_pin = APP_HW_SD_MISO;
    sd_cfg.sck_pin  = APP_HW_SD_SCK;
    sd_cfg.cs_pin   = APP_HW_SD_CS;

    int32_t rc = app_sd_init(&sd_cfg, &_card_info);
    if (rc != APP_SD_OK) {
        LOG_MSG_ERROR(MOD_SD_LOG_EN, "app_sd_init failed (%ld)", (long)rc);
        _mounted = false;
        return;
    }

    _mounted = true;
    LOG_MSG_INFO(MOD_SD_LOG_EN, "SD mounted OK — %s  %llu MB",
                 _card_info.card_type,
                 (unsigned long long)_card_info.card_size_mb);
}

void ModuleSD::post_init()
{
    if (!_mounted) {
        LOG_MSG_ERROR(MOD_SD_LOG_EN, "skipping MsgSdReady — mount failed");

        // Still publish a NOT_FOUND status so subscribers know the outcome
        MsgSdStatus::Payload sp{};
        sp.status = MsgSdStatus::SD_NOT_FOUND;
        hsys_msg_t *sm = MsgSdStatus::create(id(), sp);
        if (sm) publish(sm);
        return;
    }

    // ── MsgSdReady — "card is usable" signal ─────────────────────────────────
    LOG_MSG_INFO(MOD_SD_LOG_EN, "publishing MsgSdReady");
    hsys_msg_t *ready_msg = MsgSdReady::create(id());
    if (ready_msg) {
        publish(ready_msg);
    } else {
        LOG_MSG_ERROR(MOD_SD_LOG_EN, "failed to create MsgSdReady");
    }

    // ── MsgSdStatus — detailed card info ─────────────────────────────────────
    MsgSdStatus::Payload sp{};
    sp.status       = MsgSdStatus::SD_MOUNTED;
    sp.card_size_mb = _card_info.card_size_mb;
    strncpy(sp.card_type, _card_info.card_type, sizeof(sp.card_type) - 1);

    uint64_t free_mb = 0;
    if (app_sd_get_free_mb(&free_mb) == APP_SD_OK) {
        sp.free_mb = free_mb;
    }

    LOG_MSG_INFO(MOD_SD_LOG_EN, "SD status — type=%s  total=%llu MB  free=%llu MB",
                 sp.card_type,
                 (unsigned long long)sp.card_size_mb,
                 (unsigned long long)sp.free_mb);

    hsys_msg_t *status_msg = MsgSdStatus::create(id(), sp);
    if (status_msg) {
        publish(status_msg);
    } else {
        LOG_MSG_ERROR(MOD_SD_LOG_EN, "failed to create MsgSdStatus");
    }
}

// ── Message handler ───────────────────────────────────────────────────────────

void ModuleSD::on_msg_received(const hsys_msg_t &msg)
{
    if (msg.msg_id != MsgSdCleanup::ID) return;

    LOG_MSG_INFO(MOD_SD_LOG_EN, "MsgSdCleanup received — starting full SD wipe");

    // Blocking call: deletes every file and directory on the SD card.
    // Times out after 10 minutes.  Either way the device reboots afterward.
    int32_t rc = app_sd_cleanup(0 /* use default 10-min timeout */);

    if (rc == APP_SD_OK) {
        LOG_MSG_INFO(MOD_SD_LOG_EN, "SD wipe complete — rebooting");
    } else {
        LOG_MSG_ERROR(MOD_SD_LOG_EN, "SD wipe ended with rc=%ld — rebooting anyway", (long)rc);
    }

    hsys_msg_t *reboot_msg = MsgSystemReboot::create(id());
    if (reboot_msg) publish(reboot_msg);
}
