// msg_printer_cmd.h
//
// MsgPrinterCmd (0x0062) — Any → ModulePrinter: maintenance command
// (print sample, reset bill counter, print info slip, beep).
//
// DIRECT to ModulePrinter, which replies with MsgPrinterStatus (DIRECT to
// the sender) after the command has been executed.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "printer_types.h"
#include <stdint.h>

class MsgPrinterCmd : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_PRINTER_CMD;

    struct Payload {
        uint8_t cmd;        ///< printer_cmd_t
        uint8_t arg;        ///< command argument (BEEP: seconds)
        uint8_t _pad[2];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgPrinterCmd(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
