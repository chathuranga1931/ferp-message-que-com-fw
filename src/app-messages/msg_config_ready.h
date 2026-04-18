// msg_config_ready.h
//
// Typed message class for MSG_ID_CONFIG_READY.
//
// Published by ModuleConfig in two situations:
//   1. At boot, after the config file has been read and defaults merged.
//   2. After every successful MSG_ID_CONFIG_SET update.
//
// This is an empty notification — no payload.  Subscribers that need
// config values should call app_config_get_handle() from the application
// layer directly.  Config data is application-layer concern and must not
// leak into the message or module layers.
//
// Publisher (ModuleConfig):
//
//   hsys_msg_t *msg = MsgConfigReady::create(id());
//   publish(msg);
//
// Subscriber:
//
//   case MsgConfigReady::ID:
//       // config is ready — fetch values via app_config_get_handle()
//       break;

#ifndef MSG_CONFIG_READY_H
#define MSG_CONFIG_READY_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"    // MSG_ID_CONFIG_READY

// ---------------------------------------------------------------------------
// MsgConfigReady — empty notification
// ---------------------------------------------------------------------------

class MsgConfigReady : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_READY;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      0,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}   // nothing to serialize

    static hsys_msg_t *create(hsys_module_id_t sender_id);
};

#endif // MSG_CONFIG_READY_H
