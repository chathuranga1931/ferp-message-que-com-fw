// msg_spiffs_clean_dt_result.h
//
// MsgSpiffsCleanDtResult — reply to MsgSpiffsCleanDt, sent DIRECT by
// ModuleSpiffs to the requesting module (ModuleMqtt → resp topic).
//
// JSON:
//   {"result":0,"files":3,"bytes":316880,"t":474641,
//    "u_before":354161,"u_after":37281,"gc":0}
//
//   result    0 = all esp32/ + esp07/ files deleted, negative = a delete failed
//   files     number of files deleted
//   bytes     total size of the deleted files
//   t         SPIFFS capacity (bytes)
//   u_before  used bytes before / u_after after the cleanup
//   gc        0 = garbage collection reached its target, else it stopped early

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgSpiffsCleanDtResult : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_SPIFFS_CLEAN_DT_RESULT;

    struct Payload {
        int32_t  result      = 0;
        uint32_t files       = 0;
        uint32_t bytes       = 0;
        uint32_t total       = 0;
        uint32_t used_before = 0;
        uint32_t used_after  = 0;
        int32_t  gc          = 0;
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgSpiffsCleanDtResult(const Payload &p) : _payload(p) {}

    hsys_msg_id_t msg_id()                  const override { return ID; }
    void          serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);

    /** Response only — inbound decode not supported. */
    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *buf, uint32_t buf_len);

private:
    Payload _payload;
};
