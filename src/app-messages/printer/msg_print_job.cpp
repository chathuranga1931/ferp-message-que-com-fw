// msg_print_job.cpp

#include "msg_print_job.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define __TAG__ "MSG_PJOB"

static void _copy_str(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/** Copy a JSON value that may be a string or a number into dst. */
static void _copy_any(char *dst, size_t dst_size, JsonVariantConst v)
{
    if (v.is<const char *>()) {
        _copy_str(dst, dst_size, v.as<const char *>());
    } else if (v.is<long long>()) {
        snprintf(dst, dst_size, "%lld", v.as<long long>());
    } else if (v.is<double>()) {
        snprintf(dst, dst_size, "%.3f", v.as<double>());
    } else {
        if (dst_size) dst[0] = '\0';
    }
}

/** Number that may arrive as a JSON string ("371.00") or number. */
static double _num(JsonVariantConst v)
{
    if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        return s ? strtod(s, nullptr) : 0.0;
    }
    return v | 0.0;
}

void MsgPrintJob::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgPrintJob::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgPrintJob instance(p);
    instance.serialize(msg);
    return msg;
}

MsgPrintJob::Payload MsgPrintJob::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    // Defensive: every string is NUL-terminated whatever the sender wrote.
    p.time_stamp[sizeof(p.time_stamp) - 1] = '\0';
    p.print_time[sizeof(p.print_time) - 1] = '\0';
    p.print_note[sizeof(p.print_note) - 1] = '\0';
    p.nozzle_id[sizeof(p.nozzle_id) - 1]   = '\0';
    p.fuel_type[sizeof(p.fuel_type) - 1]   = '\0';
    p.event_id[sizeof(p.event_id) - 1]     = '\0';
    p.totalizer[sizeof(p.totalizer) - 1]   = '\0';
    return p;
}

bool MsgPrintJob::payload_from_legacy_json(const char *json, size_t len,
                                           print_job_kind_t kind, Payload *out)
{
    if (!json || !out) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json, len, DeserializationOption::NestingLimit(10)) != DeserializationError::Ok)
        return false;
    if (!doc["time"].is<const char *>()) return false;

    memset(out, 0, sizeof(*out));
    out->kind = (uint8_t)kind;
    _copy_str(out->time_stamp, sizeof(out->time_stamp), doc["time"]       | "");
    _copy_str(out->print_time, sizeof(out->print_time), doc["print_time"] | "");
    _copy_str(out->print_note, sizeof(out->print_note), doc["print_note"] | "");
    _copy_str(out->nozzle_id,  sizeof(out->nozzle_id),  doc["nozzel_id"]  | "");

    JsonVariantConst m = doc["measurements"];
    if (kind == PRINT_JOB_TOTALIZER) {
        _copy_any(out->totalizer, sizeof(out->totalizer), m["L"]);
        return true;
    }

    out->volume_l    = _num(m["L"]);
    out->unit_price  = _num(m["U"]);
    out->total_price = _num(m["P"]);
    _copy_any(out->fuel_type, sizeof(out->fuel_type), m["T"]);

    // Bill number: first of NE_ID / NID / nid that is present.
    static const char *const k_id_keys[] = { "NE_ID", "NID", "nid" };
    for (const char *k : k_id_keys) {
        if (!m[k].isNull()) {
            _copy_any(out->event_id, sizeof(out->event_id), m[k]);
            out->has_event_id = true;
            break;
        }
    }
    return true;
}

hsys_msg_t *MsgPrintJob::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;

    Payload p{};
    p.job_id      = doc["job_id"] | (uint32_t)0;
    p.kind        = doc["kind"]   | (uint8_t)PRINT_JOB_SAMPLE;
    p.source      = PRINT_SRC_MQTT;
    p.volume_l    = _num(doc["volume_l"]);
    p.unit_price  = _num(doc["unit_price"]);
    p.total_price = _num(doc["total_price"]);
    _copy_str(p.time_stamp, sizeof(p.time_stamp), doc["time"]       | "");
    _copy_str(p.print_time, sizeof(p.print_time), doc["print_time"] | "");
    _copy_str(p.print_note, sizeof(p.print_note), doc["print_note"] | "");
    _copy_str(p.nozzle_id,  sizeof(p.nozzle_id),  doc["nozzle_id"]  | "");
    _copy_str(p.fuel_type,  sizeof(p.fuel_type),  doc["fuel_type"]  | "");
    _copy_any(p.event_id,   sizeof(p.event_id),   doc["event_id"]);
    _copy_any(p.totalizer,  sizeof(p.totalizer),  doc["totalizer"]);
    p.has_event_id = p.event_id[0] != '\0';
    return create(sender_id, p);
}

int32_t MsgPrintJob::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    JsonDocument doc;
    doc["job_id"]      = p.job_id;
    doc["kind"]        = p.kind;
    doc["source"]      = p.source;
    doc["time"]        = p.time_stamp;
    doc["print_time"]  = p.print_time;
    doc["print_note"]  = p.print_note;
    doc["nozzle_id"]   = p.nozzle_id;
    doc["fuel_type"]   = p.fuel_type;
    doc["volume_l"]    = p.volume_l;
    doc["unit_price"]  = p.unit_price;
    doc["total_price"] = p.total_price;
    doc["event_id"]    = p.event_id;
    doc["totalizer"]   = p.totalizer;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0 && w < buf_len) ? 0 : -2;
}
