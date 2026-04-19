// msg_fuel_pumped.cpp

#include "msg_fuel_pumped.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_FUEL"

void MsgFuelPumped::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgFuelPumped::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgFuelPumped instance(p);
    instance.serialize(msg);
    return msg;
}

MsgFuelPumped::Payload MsgFuelPumped::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

#ifdef FERP_SIMULATOR
#include <ArduinoJson.h>
hsys_msg_t *MsgFuelPumped::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);
    Payload p{};
    p.nozzle_idx      = doc["nozzle_idx"].as<uint8_t>();
    p.vol_lx1000      = doc["vol_lx1000"].as<uint32_t>();
    p.unit_pricex100  = doc["unit_pricex100"].as<uint32_t>();
    p.total_pricex100 = doc["total_pricex100"].as<uint32_t>();
    return create(sender_id, p);
}
#endif
