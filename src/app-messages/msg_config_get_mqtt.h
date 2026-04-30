// msg_config_get_mqtt.h
//
// Typed message class for MSG_ID_CONFIG_GET_MQTT.
//
// Broadcast NOTIFICATION after MsgConfigReady.  ModuleConfig responds
// DIRECT with MsgConfigMqtt to p.source_module_id.

#ifndef MSG_CONFIG_GET_MQTT_H
#define MSG_CONFIG_GET_MQTT_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgConfigGetMqtt : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_GET_MQTT;

    struct Payload {
        hsys_module_id_t source_module_id;
        uint8_t          _pad[3];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgConfigGetMqtt(const Payload &payload) : m_payload(payload) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);

    static hsys_msg_t *mqtt_decode(const char *data_json, hsys_module_id_t sender_id);

private:
    Payload m_payload{};
};

#endif // MSG_CONFIG_GET_MQTT_H
