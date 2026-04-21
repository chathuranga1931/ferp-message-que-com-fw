// msg_sd_status.cpp

#include "msg_sd_status.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__          "MSG_SDST"
#ifndef MSG_SDST_LOG_EN
#define MSG_SDST_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------
hsys_msg_t *MsgSdStatus::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(MSG_SDST_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgSdStatus obj(payload);
    obj.serialize(msg);
    return msg;
}

// ---------------------------------------------------------------------------
// serialize()
// ---------------------------------------------------------------------------
void MsgSdStatus::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_payload, sizeof(Payload));
}

// ---------------------------------------------------------------------------
// deserialize()
// ---------------------------------------------------------------------------
MsgSdStatus::Payload MsgSdStatus::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

// ---------------------------------------------------------------------------
// Simulator inject
// ---------------------------------------------------------------------------
#ifdef FERP_SIMULATOR
#include <stdio.h>

hsys_msg_t *MsgSdStatus::from_json(const char *payload_json,
                                    hsys_module_id_t sender_id)
{
    Payload p{};
    p.status = SD_MOUNTED;

    if (payload_json) {
        int  status_raw = (int)p.status;
        unsigned long long size_raw = 0, free_raw = 0;
        char type_buf[33] = {};

        sscanf(payload_json,
               "{\"status\":%d,\"card_type\":\"%32[^\"]\","
               "\"card_size_mb\":%llu,\"free_mb\":%llu}",
               &status_raw, type_buf, &size_raw, &free_raw);

        p.status       = static_cast<SdStatus>(status_raw);
        p.card_size_mb = (uint64_t)size_raw;
        p.free_mb      = (uint64_t)free_raw;
        strncpy(p.card_type, type_buf, 31);
    }

    return create(sender_id, p);
}
#endif
