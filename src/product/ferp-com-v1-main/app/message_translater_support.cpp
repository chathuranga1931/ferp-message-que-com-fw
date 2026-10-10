// message_translater_support.cpp
//
// Application-layer message translator functions for ModuleMsgTranslator.
//
// Registered in k_translator_table[] (app.cpp):
//   MSG_ID_OTA_EVENT   → xlat_ota_event_to_system_status   → MSG_ID_SYSTEM_STATUS
//   MSG_ID_NOZZLE_STATE → xlat_nozzle_state_to_system_status → MSG_ID_SYSTEM_STATUS
//
// Both translators share static variables to maintain a single aggregated
// system busy/idle state.  The message is only published when the state
// actually changes, to avoid flooding subscribers with duplicate events.

#include "message_translater_support.h"
#include "msg_ota_event.h"
#include "msg_nozzle_state.h"
#include "msg_system_status.h"
#include "app_module_ids.h"
#include "pal_logger.h"
#include <stddef.h>

#define __TAG__  "XLAT_SUP"
#define XLOG_EN  true

// ---------------------------------------------------------------------------
// Shared static state
// ---------------------------------------------------------------------------

static bool s_is_ota_ongoing     = false;   ///< Set while an OTA session is active
static bool s_is_fueling_ongoing = false;   ///< Set while a nozzle is in PUMPING state
static bool s_is_busy            = false;   ///< Currently published state

// ---------------------------------------------------------------------------
// Internal: evaluate state and return a new message only if state changed
// ---------------------------------------------------------------------------

static hsys_msg_t *_publish_status_if_changed(hsys_msg_id_t out_msg_id,
                                               hsys_module_id_t out_dest)
{
    bool should_be_busy = (s_is_ota_ongoing || s_is_fueling_ongoing);

    if (should_be_busy && !s_is_busy) {
        s_is_busy = true;
        LOG_MSG_INFO(XLOG_EN, "system → BUSY (ota=%d fuel=%d)",
                     (int)s_is_ota_ongoing, (int)s_is_fueling_ongoing);
        MsgSystemStatus::Payload p{};
        p.status = SYSTEM_STATUS_BUSY;
        // sender_id 0 = MODULE_MSG_TRANSLATOR_ID — caller (ModuleMsgTranslator) stamps it
        return MsgSystemStatus::create((hsys_module_id_t)0, p);
    }

    if (!should_be_busy && s_is_busy) {
        s_is_busy = false;
        LOG_MSG_INFO(XLOG_EN, "system → IDLE");
        MsgSystemStatus::Payload p{};
        p.status = SYSTEM_STATUS_IDLE;
        return MsgSystemStatus::create((hsys_module_id_t)0, p);
    }

    // No state change — suppress the publish
    (void)out_msg_id; (void)out_dest;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Translator: MSG_ID_OTA_EVENT → MSG_ID_SYSTEM_STATUS
// ---------------------------------------------------------------------------

hsys_msg_t *xlat_ota_event_to_system_status(hsys_module_id_t   in_src,
                                             const hsys_msg_t  *in_msg,
                                             hsys_msg_id_t      out_msg_id,
                                             hsys_module_id_t   out_dest)
{
    (void)in_src;
    if (!in_msg) return nullptr;

    auto p = MsgOtaEvent::deserialize(*in_msg);
    s_is_ota_ongoing = (p.event == OTA_EVENT_SESSION_STARTED);

    LOG_MSG_INFO(XLOG_EN, "OTA event=%d → ota_ongoing=%d",
                 (int)p.event, (int)s_is_ota_ongoing);

    return _publish_status_if_changed(out_msg_id, out_dest);
}

// ---------------------------------------------------------------------------
// Translator: MSG_ID_NOZZLE_STATE → MSG_ID_SYSTEM_STATUS
// ---------------------------------------------------------------------------

hsys_msg_t *xlat_nozzle_state_to_system_status(hsys_module_id_t   in_src,
                                                const hsys_msg_t  *in_msg,
                                                hsys_msg_id_t      out_msg_id,
                                                hsys_module_id_t   out_dest)
{
    (void)in_src;
    if (!in_msg) return nullptr;

    auto p = MsgNozzleState::deserialize(*in_msg);
    s_is_fueling_ongoing = (p.state == NOZZLE_PUMPING);

    LOG_MSG_INFO(XLOG_EN, "nozzle[%u] state=%d -> fueling_ongoing=%d",
                 (unsigned)p.nozzle_idx, (int)p.state, (int)s_is_fueling_ongoing);

    return _publish_status_if_changed(out_msg_id, out_dest);
}
