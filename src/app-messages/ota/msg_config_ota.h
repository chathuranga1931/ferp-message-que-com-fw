// msg_config_ota.h
//
// Typed message class for MSG_ID_CONFIG_OTA.
//
// Sent DIRECT by ModuleConfig in response to MsgConfigGetOta.
// Carries the OTA-server-specific parameters needed by ModuleWebClientOta.
//
// The OTA server URL is intentionally separate from cloud_url so that
// the OTA server can live at a different host / path without affecting the
// cloud telemetry endpoint.
//
// device_uuid is included here (copied from app_config_t.device_uuid) so
// that the consumer does not need a secondary app_config_get() call.

#ifndef MSG_CONFIG_OTA_H
#define MSG_CONFIG_OTA_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgConfigOta : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_OTA;

    struct Payload {
        char        server_url [128];   ///< OTA server base URL (from app_config_t.ota_server_url)
        const char *root_ca;            ///< Pointer to PEM root-CA string (static lifetime, app_rootca.h)
        uint32_t    check_interval_s;   ///< Polling interval (seconds, clamped to 30–300 by consumer)
        uint8_t     _pad[4];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgConfigOta(const Payload &payload) : m_payload(payload) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload m_payload{};
};

#endif // MSG_CONFIG_OTA_H
