// msg_printer_cmd.cpp

#include "msg_printer_cmd.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_PCMD"

void MsgPrinterCmd::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgPrinterCmd::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgPrinterCmd instance(p);
    instance.serialize(msg);
    return msg;
}

MsgPrinterCmd::Payload MsgPrinterCmd::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

hsys_msg_t *MsgPrinterCmd::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.cmd = doc["cmd"] | (uint8_t)PRINTER_CMD_PRINT_SAMPLE;
    p.arg = doc["arg"] | (uint8_t)0;
    return create(sender_id, p);
}

int32_t MsgPrinterCmd::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    JsonDocument doc;
    doc["cmd"] = p.cmd;
    doc["arg"] = p.arg;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0 && w < buf_len) ? 0 : -2;
}
