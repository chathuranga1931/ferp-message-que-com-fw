// msg_mqtt_subscribe.h
//
// MsgMqttSubscribe — ask ModuleMqtt to (un)subscribe an extra, non-HSYS topic.
//
// Sent DIRECT to ModuleMqtt by any module that needs raw MQTT data on a topic
// that is not part of the ferp/<dev_type>/... command tree (e.g. a cloud
// notification topic learnt at runtime).  The sender becomes the topic owner:
// every message that arrives on the topic is forwarded to it as
// MsgMqttExtData (DIRECT).  ModuleMqtt keeps the subscription across
// reconnects until the owner sends the same topic with unsubscribe = true.
//
// Payloads on extra topics bypass the HSYS envelope parsing and the
// command-auth check — they are opaque bytes for the owner to interpret.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

#define MSG_MQTT_SUB_TOPIC_MAX  128   ///< incl. NUL

class MsgMqttSubscribe : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_MQTT_SUBSCRIBE;

    struct Payload {
        char    topic[MSG_MQTT_SUB_TOPIC_MAX];
        uint8_t qos;            ///< 0 or 1
        bool    unsubscribe;    ///< true → drop the subscription
        uint8_t _pad[2];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgMqttSubscribe(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
