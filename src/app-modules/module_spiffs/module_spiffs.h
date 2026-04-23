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

protected:
    // Mount SPIFFS — store result, do not publish yet
    void pre_init()  override;

    // Publish MsgSpiffsReady — all other modules have subscribed by now
    void post_init() override;

    // No runtime messages handled by this module
    void on_msg_received(const hsys_msg_t & /*msg*/) override {}

private:
    bool _mounted = false;
};
