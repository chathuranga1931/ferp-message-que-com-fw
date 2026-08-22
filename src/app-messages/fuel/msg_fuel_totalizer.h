// msg_fuel_totalizer.h
//
// Published by ModuleFuel when a nozzle's lifetime totalizer reading should
// be reported to the cloud. Distinct from MsgTotalizerData (a 16-byte display
// string used only for the long-press receipt printout) — this carries a
// numeric 64-bit volume for CubeSphere/cloud ingestion. Never retransmitted:
// if the cloud POST fails, the reading is simply dropped (best-effort, same
// as heartbeat), not queued or retried.
//
// Publisher (ModuleFuel):
//
//   MsgFuelTotalizer::Payload p{
//       .nozzle_idx  = 0,
//       .vol_lx1000  = 4123456789ULL,  // 4,123,456.789 L lifetime total
//       .time_stamp  = 1735689600,
//   };
//   auto *msg = create_typed<MsgFuelTotalizer>(p);
//   publish(msg);
//
// Subscriber (ModuleCubeSphere):
//
//   case MsgFuelTotalizer::ID: {
//       auto p = MsgFuelTotalizer::deserialize(msg);
//       // p.nozzle_idx, p.vol_lx1000, p.time_stamp
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgFuelTotalizer : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_FUEL_TOTALIZER;

    struct Payload {
        uint8_t  nozzle_idx;        ///< 0-based nozzle index
        uint8_t  _pad[3];
        uint64_t vol_lx1000;        ///< Lifetime totalizer volume in litres × 1000
        uint32_t time_stamp;        ///< Unix epoch seconds when the reading was taken
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgFuelTotalizer(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);

private:
    Payload _p;
};
