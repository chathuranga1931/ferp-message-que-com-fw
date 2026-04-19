/**
 * @file sim_msg_inject.h
 * @brief Simulator message injection — receives SIM_MSG_INJECT commands from the
 *        Python UI and injects them into the HSYS message bus.
 *
 * Called by mac_driver.cpp when a "SIM_MSG_INJECT" line arrives on the TCP link.
 *
 * Flow:
 *   mac_driver (read loop)
 *     └─ sim_msg_inject_handle(full_cmd_json)
 *           ├─ parse outer envelope  → msg_id, src_module_id, dst_module_id, payload_json
 *           ├─ switch(msg_id) → MsgXxx::from_json(payload_json, src_module_id)
 *           └─ publish (NOTIFICATION) or send (DIRECT, to dst_module_id)
 *
 * The payload JSON passed to each from_json() contains only the message-specific
 * fields (no src/dst), exactly matching the JSON objects defined in
 * src/app-messages/messages/<Category>/<msg>.json.
 *
 * This header (and its .cpp) are only ever compiled in the macOS simulator —
 * they live in pal/mac-pc/driver/ which is excluded from ESP-IDF builds.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Handle a complete SIM_MSG_INJECT command JSON line from the UI.
 *
 * @param  cmd_json  Full null-terminated JSON line, e.g.:
 *                   {"id":"SIM_MSG_INJECT","data":{"msg_id":2304,
 *                    "src_module_id":20,"dst_module_id":0,
 *                    "payload":{"status":0}}}
 */
void sim_msg_inject_handle(const char *cmd_json);

#ifdef __cplusplus
}
#endif
