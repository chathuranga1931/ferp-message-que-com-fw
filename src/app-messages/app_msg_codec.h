// app_msg_codec.h
//
// Generic JSON ↔ hsys_msg_t codec registry.
//
// Provides the registry API used by any transport module (ModuleMqtt,
// ModuleSimBridge, …) to decode inbound JSON into typed hsys_msg_t* messages
// and encode outbound hsys_msg_t* messages into JSON.
//
// The set of supported messages (the codec table) is application-specific and
// is registered from the application layer — see product/app/app.cpp.
//
// ─── Wire envelope (same structure for cmd / resp / evt) ───────────────────
//
//   {
//     "seq":  <uint32>,          // sequence number (0 for unsolicited events)
//     "msg":  "<MsgClassName>",  // C++ class name — e.g. "MsgConfigGetMqtt"
//     "data": { ... }            // message-specific payload (may be {})
//   }
//
// ─── How to add a new message ──────────────────────────────────────────────
//
//   1. Add MsgXxx::from_json() and/or MsgXxx::to_json() to the
//      message's own .cpp/.h file (see existing examples in app-messages/).
//   2. Add a row to the codec table in product/app/app.cpp.
//
// ─── Thread safety ─────────────────────────────────────────────────────────
//
//   app_msg_codec_register() must be called before any concurrent access.
//   All other functions are read-only after registration and safe from any task.

#pragma once

#include "hsys_msg.h"
#include "app_module_ids.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------

#define APP_MSG_CODEC_MSG_NAME_MAX   48    ///< Max length of a message class name string
#define APP_MSG_CODEC_DATA_JSON_MAX  512   ///< Max length of the serialised "data" object

// ---------------------------------------------------------------------------
// Codec function pointer types
// ---------------------------------------------------------------------------

/**
 * Decode the "data" JSON object of an inbound message into an hsys_msg_t
 * allocated from the HSYS pool.
 *
 * @param data_json   Null-terminated JSON string for the "data" field.
 *                    May be "{}" for messages with no payload.
 * @param sender_id   Module ID to stamp as the sender (e.g. MODULE_MQTT_ID).
 * @return            Pointer to a pool-allocated hsys_msg_t, or NULL on error.
 *                    The caller posts it to the bus; the bus owns the lifetime.
 */
typedef hsys_msg_t * (*fp_app_decode_t)(const char         *data_json,
                                         hsys_module_id_t    sender_id);

/**
 * Encode the payload of an outbound hsys_msg_t into the "data" JSON string.
 *
 * @param msg         Message to encode.  msg->payload is valid for this call.
 * @param data_json   Output buffer to write the JSON "data" object into.
 * @param buf_len     Size of data_json in bytes (including null terminator).
 * @return            0 on success, negative on error.
 */
typedef int32_t      (*fp_app_encode_t)(const hsys_msg_t   *msg,
                                         char               *data_json,
                                         uint32_t            buf_len);

// ---------------------------------------------------------------------------
// Codec entry — one row per message type that participates in JSON transport.
// This table is transport-agnostic: it only cares about serialisation.
// ---------------------------------------------------------------------------

typedef struct {
    const char        *msg_name;   ///< C++ class name, e.g. "MsgConfigGetMqtt"
    hsys_msg_id_t      msg_id;     ///< Numeric ID — used for outbound lookup by ID
    fp_app_decode_t    from_json;  ///< NULL → message is never decoded from JSON
    fp_app_encode_t    to_json;    ///< NULL → message is never encoded to JSON
} app_msg_codec_entry_t;

// ---------------------------------------------------------------------------
// MQTT route entry — one row per message that ModuleMqtt may receive inbound.
// Kept separate from the codec table so routing policy and serialisation can
// evolve independently.
// ---------------------------------------------------------------------------

typedef struct {
    hsys_msg_id_t      msg_id;         ///< Identifies the message
    hsys_module_id_t   dest_module;    ///< DIRECT destination; 0 = NOTIFICATION broadcast
    bool               multicast_resp; ///< true → process even on wildcard/group topics
} app_msg_mqtt_route_t;

// ---------------------------------------------------------------------------
// Codec registry API
// ---------------------------------------------------------------------------

/**
 * Register the codec table (name ↔ JSON serialisation mapping).
 * Must be called once at startup before any transport module is initialised.
 */
void app_msg_codec_register(const app_msg_codec_entry_t *table, uint8_t count);

/**
 * Decode an inbound JSON envelope's "msg" + "data" fields into a pool message.
 *
 * @param msg_name   Value of the "msg" field in the envelope.
 * @param data_json  Value of the "data" field as a JSON string.
 * @param sender_id  Module ID to stamp as sender.
 * @return           Pool-allocated hsys_msg_t*, or NULL on failure.
 */
hsys_msg_t *app_msg_codec_decode(const char         *msg_name,
                                  const char         *data_json,
                                  hsys_module_id_t    sender_id);

/**
 * Encode an outbound hsys_msg_t into the "msg" name and "data" JSON string.
 *
 * @param msg           Message to encode.
 * @param msg_name_out  Output buffer for the message class name string.
 * @param name_len      Size of msg_name_out.
 * @param data_json_out Output buffer for the "data" JSON object string.
 * @param data_len      Size of data_json_out.
 * @return              0 on success, negative on error.
 */
int32_t app_msg_codec_encode(const hsys_msg_t *msg,
                              char             *msg_name_out,
                              uint32_t          name_len,
                              char             *data_json_out,
                              uint32_t          data_len);

// ---------------------------------------------------------------------------
// MQTT route registry API
// ---------------------------------------------------------------------------

/**
 * Register the MQTT route table.
 * Must be called once at startup before ModuleMqtt is initialised.
 */
void app_msg_mqtt_route_register(const app_msg_mqtt_route_t *table, uint8_t count);

/**
 * Return the destination module ID for an inbound message.
 * Returns 0 (broadcast) if msg_id is not in the route table.
 */
hsys_module_id_t app_msg_mqtt_route_get_dest(hsys_msg_id_t msg_id);

/**
 * Return whether a message should be processed when received on a
 * wildcard/group topic.  Returns false if msg_id is not in the route table.
 */
bool app_msg_mqtt_route_is_multicast(hsys_msg_id_t msg_id);
