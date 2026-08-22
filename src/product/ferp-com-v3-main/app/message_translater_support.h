// message_translater_support.h
//
// Translator functions for ModuleMsgTranslator.
//
// Each function corresponds to one row in the translator table (k_translator_table[]).
// They share static state to produce a single aggregated SYSTEM_STATUS notification.
//
// Shared static state (in message_translater_support.cpp):
//   s_is_ota_ongoing     — true while an OTA session is active
//   s_is_fueling_ongoing — true while a nozzle is in the PUMPING state
//   s_is_busy            — current published state (avoids redundant publishes)
//
// Logic (publish_status):
//   If (ota || fueling) and currently IDLE → publish BUSY, update s_is_busy
//   If (!ota && !fueling) and currently BUSY → publish IDLE, update s_is_busy
//   Otherwise → return nullptr (suppress; no state change)

#pragma once

#include "ModuleMsgTranslator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Translator: MSG_ID_OTA_EVENT → MSG_ID_SYSTEM_STATUS
 *
 * Sets s_is_ota_ongoing = true  when OTA_EVENT_SESSION_STARTED.
 * Sets s_is_ota_ongoing = false for all other OTA events.
 */
hsys_msg_t *xlat_ota_event_to_system_status(
    hsys_module_id_t   in_src,
    const hsys_msg_t  *in_msg,
    hsys_msg_id_t      out_msg_id,
    hsys_module_id_t   out_dest);

/**
 * Translator: MSG_ID_NOZZLE_STATE → MSG_ID_SYSTEM_STATUS
 *
 * Sets s_is_fueling_ongoing = true  when NOZZLE_PUMPING.
 * Sets s_is_fueling_ongoing = false when NOZZLE_IDLE or NOZZLE_PUMPED.
 */
hsys_msg_t *xlat_nozzle_state_to_system_status(
    hsys_module_id_t   in_src,
    const hsys_msg_t  *in_msg,
    hsys_msg_id_t      out_msg_id,
    hsys_module_id_t   out_dest);

#ifdef __cplusplus
}
#endif
