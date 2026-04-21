// msg_config_dt.h
//
// Typed message class for MSG_ID_CONFIG_DT.
//
// DT = Device Type / hardware configuration.
// Sent DIRECT by ModuleConfig in response to MsgConfigGetDT.
// Primary consumer: ModuleFuel.

#ifndef MSG_CONFIG_DT_H
#define MSG_CONFIG_DT_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgConfigDT : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_DT;

    struct Payload {
        uint32_t display_type;          ///< display_type_t cast to uint32
        uint32_t stabilize_delay_ms;    ///< Nozzle stabilization delay
        char     printer_url[128];      ///< Receipt printer endpoint
        uint32_t printer_copy_count;    ///< Number of receipt copies
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgConfigDT(const Payload &payload) : m_payload(payload) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload m_payload{};
};

#endif // MSG_CONFIG_DT_H
