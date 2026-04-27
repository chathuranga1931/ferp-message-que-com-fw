// msg_ota_driver_response.h
//
// Sent DIRECT by OtaModule to the source in reply to MsgOtaRequestDriver.
// Carries opaque pointer to the ota_fs_driver_t and its associated context.
// Both pointers are valid for the lifetime of the OTA session (they point
// to static storage allocated in main.cpp).

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "FileSystemDriver.h"
#include <stdint.h>

class MsgOtaDriverResponse : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_OTA_DRIVER_RESPONSE;

    struct Payload {
        const ota_fs_driver_t *driver; ///< Pointer to the driver function table (static lifetime)
        void                  *ctx;    ///< Opaque context for this target (static lifetime)
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgOtaDriverResponse(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
