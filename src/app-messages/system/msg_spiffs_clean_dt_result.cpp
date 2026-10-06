// msg_spiffs_clean_dt_result.cpp

#define __TAG__  "MSG_SCDR"  // exactly 8 chars

#include "msg_spiffs_clean_dt_result.h"
#include "pal_logger.h"
#include <stdio.h>
#include <string.h>

#ifndef MSG_SCDR_LOG_EN
#define MSG_SCDR_LOG_EN true
#endif

hsys_msg_t *MsgSpiffsCleanDtResult::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(MSG_SCDR_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgSpiffsCleanDtResult obj(payload);
    obj.serialize(msg);
    return msg;
}

void MsgSpiffsCleanDtResult::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_payload, sizeof(Payload));
}

MsgSpiffsCleanDtResult::Payload MsgSpiffsCleanDtResult::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

hsys_msg_t *MsgSpiffsCleanDtResult::from_json(const char * /*data_json*/, hsys_module_id_t /*sender_id*/)
{
    return nullptr;
}

int32_t MsgSpiffsCleanDtResult::to_json(const hsys_msg_t *msg, char *buf, uint32_t buf_len)
{
    if (!msg || !buf || buf_len == 0) return -1;
    Payload p = deserialize(*msg);
    int n = snprintf(buf, buf_len,
                     "{\"result\":%ld,\"files\":%lu,\"bytes\":%lu,\"t\":%lu,"
                     "\"u_before\":%lu,\"u_after\":%lu,\"gc\":%ld}",
                     (long)p.result, (unsigned long)p.files, (unsigned long)p.bytes,
                     (unsigned long)p.total, (unsigned long)p.used_before,
                     (unsigned long)p.used_after, (long)p.gc);
    return (n > 0 && (uint32_t)n < buf_len) ? n : -2;
}
