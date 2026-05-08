// msg_config_set.cpp
//
// MsgConfigSet — binary wire-format implementation.
//
// Slab layout identical to MsgConfigValue:
//   [0-1]  key        uint16_t LE
//   [2]    type       uint8_t
//   [3]    (reserved)
//   [4-7]  data_size  uint32_t LE
//   [8+]   data bytes

#include "msg_config_set.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdint.h>

#define __TAG__ "MSG_CFGS"
#ifndef MSG_CFGS_LOG_EN
#define MSG_CFGS_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigSet::create(hsys_module_id_t sender_id,
                                   uint16_t         key,
                                   hsys_type_t      type,
                                   const void      *data,
                                   uint32_t         data_size)
{
    if (data_size > MAX_DATA) data_size = MAX_DATA;

    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(MSG_CFGS_LOG_EN, "create: pool full");
        return nullptr;
    }

    uint8_t *slab = (uint8_t *)msg->payload;
    memset(slab, 0, SLAB_SIZE);

    memcpy(slab + OFF_KEY,  &key,       2U);
    slab[OFF_TYPE] = (uint8_t)type;
    memcpy(slab + OFF_SIZE, &data_size, 4U);
    if (data && data_size > 0)
        memcpy(slab + OFF_DATA, data, data_size);

    return msg;
}

// ---------------------------------------------------------------------------
// Convenience helpers for C++ callers
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigSet::create_str(hsys_module_id_t sender_id,
                                      uint16_t         key,
                                      const char      *value)
{
    uint32_t len = (uint32_t)strnlen(value, MAX_DATA);
    return create(sender_id, key, HSYS_TYPE_STRING, value, len);
}

hsys_msg_t *MsgConfigSet::create_uint32(hsys_module_id_t sender_id,
                                         uint16_t         key,
                                         uint32_t         value)
{
    return create(sender_id, key, HSYS_TYPE_UINT32, &value, 4U);
}

hsys_msg_t *MsgConfigSet::create_bool(hsys_module_id_t sender_id,
                                       uint16_t         key,
                                       bool             value)
{
    uint8_t b = value ? 1U : 0U;
    return create(sender_id, key, HSYS_TYPE_BOOL, &b, 1U);
}

// ---------------------------------------------------------------------------
// Read helpers
// ---------------------------------------------------------------------------

uint16_t MsgConfigSet::get_key(const hsys_msg_t &msg)
{
    if (!msg.payload) return 0U;
    uint16_t k = 0;
    memcpy(&k, (uint8_t *)msg.payload + OFF_KEY, 2U);
    return k;
}

hsys_type_t MsgConfigSet::get_type(const hsys_msg_t &msg)
{
    if (!msg.payload) return HSYS_TYPE_STRING;
    return (hsys_type_t)(((uint8_t *)msg.payload)[OFF_TYPE]);
}

uint32_t MsgConfigSet::get_data_size(const hsys_msg_t &msg)
{
    if (!msg.payload) return 0U;
    uint32_t sz = 0;
    memcpy(&sz, (uint8_t *)msg.payload + OFF_SIZE, 4U);
    return sz;
}

const void *MsgConfigSet::get_data(const hsys_msg_t &msg)
{
    if (!msg.payload) return nullptr;
    return (uint8_t *)msg.payload + OFF_DATA;
}

// ---------------------------------------------------------------------------
// from_json — decode binary array from HTTP request
// {"key":4097,"type":0,"size":9,"data":[77,121,...]}
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigSet::from_json(const char *data_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, data_json) != DeserializationError::Ok) return nullptr;

    uint16_t    key       = (uint16_t)(doc["key"]  | 0);
    hsys_type_t type      = (hsys_type_t)(uint8_t)(doc["type"] | 0);
    JsonArray   arr       = doc["data"].as<JsonArray>();
    uint32_t    data_size = (uint32_t)arr.size();

    uint8_t buf[MAX_DATA] = {};
    uint32_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= MAX_DATA) break;
        buf[i++] = (uint8_t)(v.as<unsigned int>());
    }

    return create(sender_id, key, type, buf, data_size);
}

// ---------------------------------------------------------------------------
// to_json — encode binary slab as JSON byte array for transport
// ---------------------------------------------------------------------------

int32_t MsgConfigSet::to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len)
{
    if (!msg || !msg->payload) return -1;

    uint16_t    key       = get_key(*msg);
    uint8_t     type      = (uint8_t)get_type(*msg);
    uint32_t    data_size = get_data_size(*msg);
    const uint8_t *data   = (const uint8_t *)get_data(*msg);

    StaticJsonDocument<768> doc;
    doc["key"]  = key;
    doc["type"] = type;
    doc["size"] = data_size;

    JsonArray arr = doc.createNestedArray("data");
    for (uint32_t i = 0; i < data_size; i++)
        arr.add(data[i]);

    size_t w = serializeJson(doc, data_json_out, buf_len);
    return (w > 0) ? 0 : -2;
}
