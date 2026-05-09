// module_device_info.cpp
//
// ModuleDeviceInfo — manages runtime device identity.

#include "module_device_info.h"
#include "app_device_info.h"
#include "msg_dev_info_read.h"
#include "msg_dev_info_write.h"
#include "msg_dev_info_value.h"
#include "app_msg_ids.h"
#include "pal_logger.h"
#include "pal_efuse.h"
#include <stdio.h>
#include <string.h>

#define __TAG__           "MOD_DINF"
#ifndef MOD_DINF_LOG_EN
#define MOD_DINF_LOG_EN  true
#endif

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModuleDeviceInfo s_instance;

ModuleDeviceInfo *ModuleDeviceInfo::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

void ModuleDeviceInfo::init()
{
    // Populate hw_address from eFuse MAC before the info table is registered,
    // so the field is valid from the very first read.
    {
        uint8_t mac[6] = {};
        if (pal_efuse_get_mac(mac, sizeof(mac)) == PAL_OK) {
            app_device_info_t *di = app_device_info_get();
            snprintf(di->hw_address, sizeof(di->hw_address),
                     "%02x%02x%02x%02x%02x%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            // Mark the hw_address table entry valid
            uint16_t count = 0;
            dev_info_entry_t *table = app_device_info_get_table(&count);
            for (uint16_t i = 0; i < count; i++) {
                if (table[i].key == DEV_INFO_KEY_HW_ADDRESS) {
                    table[i].is_valid = true;
                    break;
                }
            }
            LOG_MSG_INFO(MOD_DINF_LOG_EN, "hw_address: %s", di->hw_address);
        } else {
            LOG_MSG_ERROR(MOD_DINF_LOG_EN, "pal_efuse_get_mac failed");
        }
    }

    uint16_t count = 0;
    dev_info_entry_t *table = app_device_info_get_table(&count);

    dev_info_init_t init_cfg = { count, table };
    int32_t rc = hsys_dev_info_init(init_cfg, &m_handle);
    if (rc != DEV_INFO_SUCCESS) {
        LOG_MSG_ERROR(MOD_DINF_LOG_EN, "hsys_dev_info_init failed (%ld)", (long)rc);
    }

    subscribe(MSG_ID_DEV_INFO_WRITE);
    subscribe(MSG_ID_DEV_INFO_READ);

    LOG_MSG_INFO(MOD_DINF_LOG_EN, "init done — %u entries", (unsigned)count);
}

// ---------------------------------------------------------------------------
// on_msg_received()
// ---------------------------------------------------------------------------

void ModuleDeviceInfo::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MSG_ID_DEV_INFO_WRITE:
            _handle_write(msg);
            break;

        case MSG_ID_DEV_INFO_READ:
            _handle_read(msg);
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Private: handle write
// ---------------------------------------------------------------------------

void ModuleDeviceInfo::_handle_write(const hsys_msg_t &msg)
{
    auto p = MsgDevInfoWrite::deserialize(msg);

    const void *src      = nullptr;
    size_t      src_len  = 0;

    switch (p.type) {
        case HSYS_TYPE_STRING:
            src     = p.value.as_str;
            src_len = strlen(p.value.as_str) + 1;
            break;
        case HSYS_TYPE_UINT32:
            src     = &p.value.as_uint32;
            src_len = sizeof(p.value.as_uint32);
            break;
        case HSYS_TYPE_BOOL:
            src     = &p.value.as_bool;
            src_len = sizeof(p.value.as_bool);
            break;
        default:
            LOG_MSG_WARNING(MOD_DINF_LOG_EN, "write: unknown type %u", (unsigned)p.type);
            return;
    }

    int32_t rc = hsys_dev_info_write(&m_handle, msg.sender_id, p.key, src, src_len);
    if (rc == DEV_INFO_PERM_DENIED) {
        LOG_MSG_WARNING(MOD_DINF_LOG_EN,
                        "write key=0x%04X denied for module %u",
                        (unsigned)p.key, (unsigned)msg.sender_id);
    } else if (rc != DEV_INFO_SUCCESS) {
        LOG_MSG_ERROR(MOD_DINF_LOG_EN,
                      "write key=0x%04X failed (%ld)", (unsigned)p.key, (long)rc);
    } else {
        LOG_MSG_INFO(MOD_DINF_LOG_EN,
                     "wrote key=0x%04X from module %u", (unsigned)p.key, (unsigned)msg.sender_id);
        // Broadcast the new value so subscribers (e.g. ModuleMqtt) can react
        MsgDevInfoValue::Payload notif{};
        notif.key      = p.key;
        notif.type     = p.type;
        notif.is_valid = true;
        switch (p.type) {
            case HSYS_TYPE_STRING:
                snprintf(notif.value.as_str, sizeof(notif.value.as_str),
                         "%s", p.value.as_str);
                break;
            case HSYS_TYPE_UINT32:
                notif.value.as_uint32 = p.value.as_uint32;
                break;
            case HSYS_TYPE_BOOL:
                notif.value.as_bool = p.value.as_bool;
                break;
            default:
                break;
        }
        hsys_msg_t *notif_msg = MsgDevInfoValue::create(id(), (hsys_module_id_t)0, notif);
        if (notif_msg) publish(notif_msg);
    }
}

// ---------------------------------------------------------------------------
// Private: handle read
// ---------------------------------------------------------------------------

void ModuleDeviceInfo::_handle_read(const hsys_msg_t &msg)
{
    auto p = MsgDevInfoRead::deserialize(msg);

    MsgDevInfoValue::Payload resp{};
    resp.key      = p.key;
    resp.is_valid = false;

    hsys_type_t out_type = HSYS_TYPE_STRING;
    int32_t rc = hsys_dev_info_read(&m_handle, p.source_module_id, p.key,
                                    resp.value.as_str, sizeof(resp.value.as_str),
                                    &out_type);

    resp.type = out_type;

    if (rc == DEV_INFO_PERM_DENIED) {
        LOG_MSG_WARNING(MOD_DINF_LOG_EN,
                        "read key=0x%04X denied for module %u",
                        (unsigned)p.key, (unsigned)p.source_module_id);
        // Still send response so requester isn't stuck — is_valid = false signals failure.
    } else if (rc == DEV_INFO_NOT_VALID) {
        LOG_MSG_INFO(MOD_DINF_LOG_EN, "read key=0x%04X: not yet valid", (unsigned)p.key);
    } else if (rc != DEV_INFO_SUCCESS) {
        LOG_MSG_ERROR(MOD_DINF_LOG_EN,
                      "read key=0x%04X failed (%ld)", (unsigned)p.key, (long)rc);
    } else {
        resp.is_valid = true;
        LOG_MSG_INFO(MOD_DINF_LOG_EN,
                     "read key=0x%04X -> module %u", (unsigned)p.key, (unsigned)p.source_module_id);
    }

    hsys_msg_t *out = MsgDevInfoValue::create(id(), p.source_module_id, resp);
    if (out) {
        send(out, p.source_module_id);
    } else {
        LOG_MSG_ERROR(MOD_DINF_LOG_EN, "failed to create MsgDevInfoValue");
    }
}
