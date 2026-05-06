// msg_http_set_stream_sink.h
//
// Sent DIRECT by the session owner to ModuleHttp immediately before
// MsgHttpSendRequest to configure a streaming binary write sink.
//
// When a stream sink is set, ModuleHttp uses pal_http_client_get_stream()
// instead of pal_http_client_get() and pipes each received chunk directly
// into the ota_fs_driver_t write session:
//   fopen  → fwrite × N → fclose     on success
//   fopen  → fwrite × N → ferase     on transport or CRC error
//
// The fs_drv and fs_ctx pointers must remain valid until MsgHttpResult is
// received (i.e. for the entire duration of the HTTP session).
//
// expected_crc32: if non-zero, ModuleHttp verifies the CRC32 of the received
// data (poly 0xEDB88320, initial 0, no final XOR) and sends
// HTTP_RESULT_ERROR on mismatch.  Set to 0 to skip CRC verification.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"
#include "FileSystemDriver.h"   /* ota_fs_driver_t */
#include <stdint.h>

class MsgHttpSetStreamSink : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_SET_STREAM_SINK;

    struct Payload {
        const ota_fs_driver_t *fs_drv;         ///< OTA filesystem driver (static/session lifetime)
        void                  *fs_ctx;         ///< Driver context passed verbatim to each driver op
        uint32_t               expected_crc32; ///< Expected CRC32 of firmware (0 = skip check)
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgHttpSetStreamSink(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
