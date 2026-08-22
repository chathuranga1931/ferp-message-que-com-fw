// module_spiffs.h
//
// ModuleSpiffs — HSYS module that mounts SPIFFS and announces readiness.
//
// Lifecycle:
//   pre_init()   — calls app_spiffs_init(); stores result
//   post_init()  — publishes MsgSpiffsReady (all subscribers registered by now)
//
// Subscribers that depend on the filesystem (e.g. ModuleConfig) subscribe
// to MSG_ID_SPIFFS_READY and wait for this message before reading files.

#pragma once

#include "hsys_module.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_SPIFFS_NAME  "spiffs"

// ---------------------------------------------------------------------------
// ModuleSpiffs
// ---------------------------------------------------------------------------

class ModuleSpiffs : public HsysModule
{
public:
    ModuleSpiffs() : HsysModule(MODULE_SPIFFS_ID, MODULE_SPIFFS_NAME) {}

    /** Return the singleton instance used in k_module_table[]. */
    static ModuleSpiffs *instance();

    /**
     * Supply a list of stale SPIFFS files to purge immediately after mount.
     *
     * Intended for cross-product firmware transitions: a build carries the
     * paths belonging to the *other* product's secondary MCU (e.g. an esp07
     * build lists the esp32 DispTap binaries) so they are reclaimed before
     * they can overflow the SPIFFS partition during a later OTA.
     *
     * Call from app_init() before hsys_module_init() (pre_init() reads it).
     * The array must have static lifetime — only the pointer is stored.
     * Missing files are ignored, so listing paths that don't exist is safe.
     */
    void set_stale_files(const char *const *paths, uint8_t count);

protected:
    // Mount SPIFFS — store result, do not publish yet
    void pre_init()  override;

    // Publish MsgSpiffsReady — all other modules have subscribed by now
    void post_init() override;

    // Handle MsgSpiffsCleanup
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    bool _mounted = false;

    // Stale-file purge list supplied via set_stale_files() (static lifetime).
    const char *const *_stale_files = nullptr;
    uint8_t            _stale_count = 0;

    // Deletes each _stale_files[] entry that exists; runs once after mount.
    void _purge_stale_files();

    // Saves config to RAM, formats SPIFFS, writes config back, triggers reboot
    void _on_spiffs_cleanup();
};
