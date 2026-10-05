// msg_printer_status.h
//
// MsgPrinterStatus (0x0064) — ModulePrinter → requester: printer state.
//
// DIRECT reply to MsgPrinterGetStatus and MsgPrinterCmd.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "printer_types.h"
#include <stdint.h>

class MsgPrinterStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_PRINTER_STATUS;

    struct Payload {
        char     transport[8];      ///< "uart" | "usb"
        bool     ready;             ///< transport can accept data now
        uint8_t  last_result;       ///< print_result_t of the last job
        uint8_t  last_cmd;          ///< printer_cmd_t executed (0xFF = status request)
        uint8_t  _pad;
        uint32_t print_count;       ///< bill counter (persisted)
        uint32_t jobs_ok;           ///< since boot
        uint32_t jobs_failed;       ///< since boot
        uint32_t last_job_id;
        char     detail[40];        ///< transport detail, e.g. "9600 baud" or USB product string
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgPrinterStatus(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
