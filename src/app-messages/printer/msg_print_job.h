// msg_print_job.h
//
// MsgPrintJob (0x0060) — Any → ModulePrinter: print one receipt / slip.
//
// DIRECT to ModulePrinter.  ModulePrinter replies with MsgPrintResult
// (DIRECT to the sender) once the job has been written to the printer.
//
// Field names follow the legacy printer HTTP API (/print body):
//   time        → time_stamp   (fueled time, ISO "YYYY-MM-DDTHH:MM:SS")
//   print_time  → print_time   (bill issued time, printed as-is)
//   print_note  → print_note   ("Original", "Copy", "manual", …)
//   nozzel_id   → nozzle_id
//   T / fuel_type, L, U, P, NE_ID|NID|nid → fuel_type, volume_l,
//                                            unit_price, total_price, event_id

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "printer_types.h"
#include <stdint.h>

class MsgPrintJob : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_PRINT_JOB;

    struct Payload {
        uint32_t job_id;            ///< caller tag, echoed in MsgPrintResult
        uint8_t  kind;              ///< print_job_kind_t
        uint8_t  source;            ///< print_job_source_t
        bool     has_event_id;      ///< event_id came from the request (else bill counter is used)
        uint8_t  _pad;
        double   volume_l;
        double   unit_price;
        double   total_price;
        char     time_stamp[32];
        char     print_time[32];
        char     print_note[24];
        char     nozzle_id[16];
        char     fuel_type[24];
        char     event_id[40];
        char     totalizer[24];     ///< totalizer volume, printed verbatim
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgPrintJob(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

    /**
     * Fill a payload from a legacy /print or /print-totalizer JSON body
     * ({"time","print_time","print_note","nozzel_id","measurements":{...}}).
     * Returns false when the mandatory "time" field is missing.
     */
    static bool payload_from_legacy_json(const char *json, size_t len,
                                         print_job_kind_t kind, Payload *out);

    /** JSON codec (MQTT / web bridge). */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
