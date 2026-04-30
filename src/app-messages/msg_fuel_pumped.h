// msg_fuel_pumped.h
//
// Published by ModuleFuel when a complete fueling transaction is confirmed.
//
// Publisher (ModuleFuel):
//
//   MsgFuelPumped::Payload p{
//       .nozzle_idx      = 0,
//       .vol_lx1000      = 5000,     // 5.000 L
//       .unit_pricex100  = 300,      // $3.00
//       .total_pricex100 = 1500,     // $15.00
//   };
//   auto *msg = create_typed<MsgFuelPumped>(p);
//   publish(msg);
//
// Subscriber (UI bridge, cloud module, etc.):
//
//   case MsgFuelPumped::ID: {
//       auto p = MsgFuelPumped::deserialize(msg);
//       // p.nozzle_idx, p.vol_lx1000, p.unit_pricex100, p.total_pricex100
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgFuelPumped : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_FUEL_PUMPED;

    struct Payload {
        uint8_t  nozzle_idx;        ///< 0-based nozzle index
        uint8_t  _pad[3];
        uint32_t vol_lx1000;        ///< Volume in litres × 1000  (e.g. 5000 = 5.000 L)
        uint32_t unit_pricex100;    ///< Unit price × 100         (e.g. 300 = $3.00)
        uint32_t total_pricex100;   ///< Total price × 100        (e.g. 1500 = $15.00)
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgFuelPumped(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

#ifdef FERP_SIMULATOR
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
#endif

private:
    Payload _p;
};
