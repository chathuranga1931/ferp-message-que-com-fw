// msg_mqtt_ext_data.h
//
// MsgMqttExtData — raw MQTT data received on an extra topic.
//
// Sent DIRECT by ModuleMqtt to the module that registered the topic with
// MsgMqttSubscribe.  The payload is copied verbatim (truncated to
// MSG_MQTT_EXT_DATA_MAX bytes, always NUL-terminated so text payloads can be
// used as C strings).

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

#define MSG_MQTT_EXT_TOPIC_MAX   96   ///< incl. NUL (longer topics are truncated)
#define MSG_MQTT_EXT_DATA_MAX   156   ///< incl. NUL

class MsgMqttExtData : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_MQTT_EXT_DATA;

    struct Payload {
        char     topic[MSG_MQTT_EXT_TOPIC_MAX];
        uint16_t data_len;          ///< bytes in data (excl. NUL)
        bool     truncated;         ///< original payload was longer than data[]
        uint8_t  _pad;
        char     data[MSG_MQTT_EXT_DATA_MAX];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgMqttExtData(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    /** Build from raw topic/data buffers (neither needs to be NUL-terminated). */
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                              const char *topic, size_t topic_len,
                              const char *data,  size_t data_len);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
