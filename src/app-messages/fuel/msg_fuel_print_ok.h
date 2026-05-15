// msg_fuel_print_ok.h
//
// Published when a receipt print completes successfully for a nozzle
// transaction.  ModuleCubeSphere subscribes to this and sends the
// app.fuel/printok cloud event.
//
// Publisher example (e.g. HTTP print module or receipt module):
//
//   MsgFuelPrintOk::Payload p{};
//   p.nozzle_idx          = 0;
//   p.dispenser_event_id  = ev.event_id;   // ABS_ID from the dispenser
//   p.timestamp_epoch     = now_tv.tv_sec;
//   auto *msg = create_typed<MsgFuelPrintOk>(p);
//   publish(msg);
//
// Subscriber (ModuleCubeSphere):
//
//   case MsgFuelPrintOk::ID: {
//       auto p = MsgFuelPrintOk::deserialize(msg);
//       // p.nozzle_idx, p.dispenser_event_id, p.timestamp_epoch
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

#include <stdint.h>
#include <time.h>

class MsgFuelPrintOk : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_FUEL_PRINT_OK;

    struct Payload {
        uint8_t  nozzle_idx;           ///< 0-based nozzle index
        uint8_t  _pad[3];
        uint32_t dispenser_event_id;   ///< ABS_ID: absolute event counter from dispenser
        int64_t  timestamp_epoch;      ///< Unix epoch seconds of the transaction
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgFuelPrintOk(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);

private:
    Payload _p;
};
