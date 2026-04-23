// module_sd.h
//
// ModuleSD — HSYS module that mounts the SD card via app_sd and announces
// readiness to the rest of the system.
//
// Lifecycle:
//   pre_init()   — calls app_sd_init(); stores result + card info
//   post_init()  — publishes MsgSdReady (all subscribers registered by now)
//
// Subscribers that depend on SD storage subscribe to MSG_ID_SD_READY
// and wait for this message before accessing the card.

#pragma once

#include "hsys_module.h"
#include "app_sd.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_SD_NAME  "mod_sd"

// ---------------------------------------------------------------------------
// ModuleSD
// ---------------------------------------------------------------------------

class ModuleSD : public HsysModule
{
public:
    ModuleSD() : HsysModule(MODULE_SD_ID, MODULE_SD_NAME) {}

    static ModuleSD *instance();

protected:
    void pre_init()  override;
    void post_init() override;
    void on_msg_received(const hsys_msg_t & /*msg*/) override {}

private:
    bool         _mounted   = false;
    app_sd_info_t _card_info = {};
};
