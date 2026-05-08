// msg_config_value.cpp

#include "msg_config_value.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_CFGV"
#ifndef MSG_CFGV_LOG_EN
#define MSG_CFGV_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigValue::create(hsys_module_id_t sender_id,
                                    hsys_module_id_t receiver_id,
                                    uint16_t         key,
                                    hsys_type_t      type,
                                    const void      *data,
                                    uint32_t         data_size)
{
    if (data_size > MAX_DATA) data_size = MAX_DATA;

    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(MSG_CFGV_LOG_EN, "create: pool full");
        return nullptr;
    }

    msg->receiver_id = receiver_id;

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
// Read helpers
// ---------------------------------------------------------------------------

uint16_t MsgConfigValue::get_key(const hsys_msg_t &msg)
{
    if (!msg.payload) return 0U;
    uint16_t k = 0;
    memcpy(&k, (uint8_t *)msg.payload + OFF_KEY, 2U);
    return k;
}

hsys_type_t MsgConfigValue::get_type(const hsys_msg_t &msg)
{
    if (!msg.payload) return HSYS_TYPE_STRING;
    return (hsys_type_t)(((uint8_t *)msg.payload)[OFF_TYPE]);
}

uint32_t MsgConfigValue::get_data_size(const hsys_msg_t &msg)
{
    if (!msg.payload) return 0U;
    uint32_t sz = 0;
    memcpy(&sz, (uint8_t *)msg.payload + OFF_SIZE, 4U);
    return sz;
}

const void *MsgConfigValue::get_data(const hsys_msg_t &msg)
{
    if (!msg.payload) return nullptr;
    return (uint8_t *)msg.payload + OFF_DATA;
}

// ---------------------------------------------------------------------------
// to_json — encodes the binary slab into JSON byte array
// {"key":4097,"type":0,"size":9,"data":[77,121,...]}
// ---------------------------------------------------------------------------

int32_t MsgConfigValue::to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len)
{
    if (!msg || !msg->payload) return -1;

    uint16_t    key       = get_key(*msg);
    uint8_t     type      = (uint8_t)get_type(*msg);
    uint32_t    data_size = get_data_size(*msg);
    const uint8_t *data   = (const uint8_t *)get_data(*msg);

    // Use a dynamic-size JsonDocument — data array can be up to 248 entries
    // 512 bytes is enough for all realistic config values
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

// ---------------------------------------------------------------------------
// from_json — decodes JSON byte array back into a slab message
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigValue::from_json(const char *data_json, hsys_module_id_t sender_id)
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

    return create(sender_id, sender_id, key, type, buf, data_size);
}
