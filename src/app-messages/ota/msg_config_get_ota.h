// msg_config_get_ota.h
//
// Typed message class for MSG_ID_CONFIG_GET_OTA.
//
// NOTIFICATION published by any module that needs the OTA server configuration.
// ModuleConfig responds DIRECT with MsgConfigOta to p.source_module_id.
// The OTA server URL is stored in app_config_t.ota_server_url, which is
// a separate endpoint from the cloud API URL (app_config_t.cloud_url).

#ifndef MSG_CONFIG_GET_OTA_H
#define MSG_CONFIG_GET_OTA_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgConfigGetOta : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_GET_OTA;

    struct Payload {
        hsys_module_id_t source_module_id;  ///< Requester module ID — response sent here
        uint8_t          _pad[3];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgConfigGetOta(const Payload &payload) : m_payload(payload) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);

    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len);

private:
    Payload m_payload{};
};

#endif // MSG_CONFIG_GET_OTA_H
