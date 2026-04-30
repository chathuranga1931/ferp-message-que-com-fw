// msg_config_mqtt.h
//
// Typed message class for MSG_ID_CONFIG_MQTT.
//
// Sent DIRECT by ModuleConfig in response to MsgConfigGetMqtt.
// Carries MQTT broker connection parameters.

#ifndef MSG_CONFIG_MQTT_H
#define MSG_CONFIG_MQTT_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgConfigMqtt : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_MQTT;

    struct Payload {
        char     host[64];      ///< Broker hostname or IP
        uint32_t port;          ///< Broker port (e.g. 1883 / 8883)
        char     user[32];      ///< Username (empty string = no auth)
        char     password[64];  ///< Password
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgConfigMqtt(const Payload &payload) : m_payload(payload) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload m_payload{};
};

#endif // MSG_CONFIG_MQTT_H
