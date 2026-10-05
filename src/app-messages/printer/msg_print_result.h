// msg_print_result.h
//
// MsgPrintResult (0x0061) — ModulePrinter → job sender: outcome of a MsgPrintJob.
//
// DIRECT to the module that sent the job (ModuleCloudPrint uses it to decide
// between ACK and retry; ModuleMqtt forwards it to the resp topic).

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "printer_types.h"
#include <stdint.h>

class MsgPrintResult : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_PRINT_RESULT;

    struct Payload {
        uint32_t job_id;        ///< echoed from MsgPrintJob
        uint8_t  result;        ///< print_result_t
        uint8_t  source;        ///< print_job_source_t
        uint8_t  _pad[2];
        uint32_t print_count;   ///< bill counter after this job
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgPrintResult(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
