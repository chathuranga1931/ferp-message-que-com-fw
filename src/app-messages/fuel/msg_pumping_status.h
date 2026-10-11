// msg_pumping_status.h
//
// Published by ModuleFuel while a nozzle is pumping, every N seconds
// (CFG_KEY_MQTT_PUMP_STATUS_INTERVAL_S, min 5 s), when
// CFG_KEY_MQTT_PUMP_STATUS_EN is on.  One message per pumping nozzle.
// Live display values — not a completed sale (that is MsgFuelPumped).
// ModuleMqtt forwards it as an evt; the cloud and printing ignore it.
//
// Publisher (ModuleFuel):
//
//   MsgPumpingStatus::Payload p{};
//   p.nozzle_idx      = 0;
//   p.vol_lx1000      = 1250;    // 1.250 L so far
//   p.unit_pricex100  = 30900;   // 309.00
//   p.total_pricex100 = 38625;   // 386.25 so far
//   p.time_stamp      = epoch;
//   publish(create_typed<MsgPumpingStatus>(p));

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgPumpingStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_FUEL_PUMPING_STATUS;

    struct Payload {
        uint8_t  nozzle_idx;        ///< 0-based nozzle index
        uint8_t  _pad[3];
        uint32_t vol_lx1000;        ///< Volume so far in litres × 1000
        uint32_t unit_pricex100;    ///< Unit price × 100
        uint32_t total_pricex100;   ///< Total so far × 100
        uint32_t time_stamp;        ///< Unix epoch seconds
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgPumpingStatus(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);

private:
    Payload _p;
};
