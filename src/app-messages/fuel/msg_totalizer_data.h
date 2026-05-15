// msg_totalizer_data.h
//
// Published when a nozzle totalizer reading is available (e.g. received from
// the dispenser on long-press).  ModulePrinting subscribes to this and uses
// it to serve the long-press totalizer print.
//
// Publisher example:
//
//   MsgTotalizerData::Payload p{};
//   p.nozzle_idx      = 0;
//   p.timestamp_epoch = (uint32_t)now;
//   strncpy(p.totalizer_str, "000005.000", sizeof(p.totalizer_str) - 1);
//   auto *msg = create_typed<MsgTotalizerData>(p);
//   publish(msg);
//
// Subscriber:
//
//   case MsgTotalizerData::ID: {
//       auto p = MsgTotalizerData::deserialize(msg);
//       // p.nozzle_idx, p.timestamp_epoch, p.totalizer_str
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

#include <stdint.h>

class MsgTotalizerData : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_TOTALIZER_DATA;

    struct Payload {
        uint8_t  nozzle_idx;          ///< 0-based nozzle index
        uint8_t  _pad[3];
        uint32_t timestamp_epoch;     ///< Unix epoch seconds when reading was taken
        char     totalizer_str[16];   ///< NUL-terminated volume string e.g. "000005.000"
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgTotalizerData(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);

private:
    Payload _p;
};
