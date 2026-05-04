// module_device_info.h
//
// ModuleDeviceInfo — owns the device identity table lifecycle.
//
// Responsibilities:
//   - Initialises the hsys_device_info handle from app_device_info_get_table().
//   - Handles MSG_ID_DEV_INFO_WRITE: validates write permission, stores value.
//   - Handles MSG_ID_DEV_INFO_READ:  validates read permission, sends
//     MsgDevInfoValue directly back to the requester.
//
// Write permission is enforced in hsys_device_info.cpp using the per-field
// write-permission tables defined in app.cpp.

#pragma once

#include "hsys_module.h"
#include "hsys_device_info.h"
#include "app_module_ids.h"

#define MODULE_DEVICE_INFO_NAME  "dev_info"

class ModuleDeviceInfo : public HsysModule
{
public:
    ModuleDeviceInfo()
        : HsysModule(MODULE_DEVICE_INFO_ID, MODULE_DEVICE_INFO_NAME) {}

    static ModuleDeviceInfo *instance();

protected:
    void init()                                    override;
    void on_msg_received(const hsys_msg_t &msg)    override;

private:
    dev_info_handle_t m_handle {};

    void _handle_write(const hsys_msg_t &msg);
    void _handle_read (const hsys_msg_t &msg);
};
