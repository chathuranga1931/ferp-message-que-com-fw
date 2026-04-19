// msg_nozzle_state.h
//
// Published by ModuleFuel whenever a nozzle transitions between
// IDLE ↔ PUMPING ↔ PUMPED states.
//
// Publisher (ModuleFuel):
//
//   MsgNozzleState::Payload p{ .nozzle_idx = 0, .state = NOZZLE_PUMPING };
//   auto *msg = create_typed<MsgNozzleState>(p);
//   publish(msg);
//
// Subscriber:
//
//   case MsgNozzleState::ID: {
//       auto p = MsgNozzleState::deserialize(msg);
//       // p.nozzle_idx, p.state
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "nozzle_event.h"   // nozzle_state_t

class MsgNozzleState : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_NOZZLE_STATE;

    struct Payload {
        uint8_t        nozzle_idx;   ///< 0-based nozzle index
        uint8_t        _pad[3];
        nozzle_state_t state;        ///< New state
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgNozzleState(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

#ifdef FERP_SIMULATOR
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
#endif

private:
    Payload _p;
};
