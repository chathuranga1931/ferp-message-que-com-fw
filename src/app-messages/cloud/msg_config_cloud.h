// msg_config_cloud.h
//
// Typed message class for MSG_ID_CONFIG_CLOUD.
//
// Sent DIRECT by ModuleConfig in response to MsgConfigGetCloud.
// Carries cloud-specific parameters needed by ModuleCloud.
// WiFi credentials (ssid, password) travel via MsgConfigWifi.
// The device MAC address is sourced from MsgWifiEvent at runtime.

#ifndef MSG_CONFIG_CLOUD_H
#define MSG_CONFIG_CLOUD_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>
#include <stdbool.h>

class MsgConfigCloud : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_CLOUD;

    struct Payload {
        const char *root_ca;          ///< Pointer to PEM root-CA string (static lifetime, e.g. app_rootca.h)
        bool        hb_enabled;       ///< Heartbeat enabled flag
        uint8_t     _pad[3];
        uint32_t    hb_interval_s;    ///< Heartbeat interval in seconds
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgConfigCloud(const Payload &payload) : m_payload(payload) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload m_payload{};
};

#endif // MSG_CONFIG_CLOUD_H
