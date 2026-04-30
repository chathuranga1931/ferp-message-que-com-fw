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
//   1. Add MsgXxx::mqtt_decode() and/or MsgXxx::mqtt_encode() to the
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
// Codec entry — one row per message type that participates in JSON transport
// ---------------------------------------------------------------------------

typedef struct {
    const char        *msg_name;    ///< C++ class name, e.g. "MsgConfigGetMqtt"
    hsys_msg_id_t      msg_id;      ///< Numeric ID — used for outbound lookup by ID
    hsys_module_id_t   dest_module;    ///< DIRECT destination for inbound; 0 = NOTIFICATION broadcast
    fp_app_decode_t    decode;         ///< NULL → device never receives this msg via JSON transport
    fp_app_encode_t    encode;         ///< NULL → device never sends this msg via JSON transport
    bool               multicast_resp; ///< true → respond even when cmd arrived via a wildcard/group topic
                                       ///<        false (default) → only respond to exact-topic delivery
} app_msg_codec_entry_t;

// ---------------------------------------------------------------------------
// Registry API
// ---------------------------------------------------------------------------

/**
 * Register the codec table.
 * Must be called once at startup before any transport module is initialised.
 * Subsequent calls replace the previous table.
 */
void app_msg_codec_register(const app_msg_codec_entry_t *table, uint8_t count);

/**
 * Decode an inbound JSON envelope's "msg" + "data" fields.
 *
 * Looks up msg_name in the registered table, calls the decoder, and returns
 * a pool-allocated hsys_msg_t ready to post to the bus.
 *
 * @param msg_name   Value of the "msg" field in the envelope.
 * @param data_json  Value of the "data" field as a JSON string.
 * @param sender_id  Sender ID to stamp on the message.
 * @return           New hsys_msg_t*, or NULL if msg_name unknown or decode failed.
 */
hsys_msg_t *app_msg_codec_decode(const char         *msg_name,
                                  const char         *data_json,
                                  hsys_module_id_t    sender_id);

/**
 * Return the destination module ID for an inbound message name.
 * Returns 0 if msg_name is unknown or the message is a NOTIFICATION (broadcast).
 */
hsys_module_id_t app_msg_codec_get_dest(const char *msg_name);

/**
 * Return whether the named inbound message should be processed when received
 * via a wildcard/group topic (multicast delivery).
 * Returns false if msg_name is unknown.
 */
bool app_msg_codec_is_multicast(const char *msg_name);

/**
 * Encode an outbound hsys_msg_t into the "msg" name and "data" JSON string.
 *
 * @param msg           Message to encode.
 * @param msg_name_out  Output buffer for the message class name string.
 * @param name_len      Size of msg_name_out.
 * @param data_json_out Output buffer for the "data" JSON object string.
 * @param data_len      Size of data_json_out.
 * @return              0 on success, -1 if msg->id unknown, -2 if encode failed.
 */
int32_t app_msg_codec_encode(const hsys_msg_t *msg,
                              char             *msg_name_out,
                              uint32_t          name_len,
                              char             *data_json_out,
                              uint32_t          data_len);
