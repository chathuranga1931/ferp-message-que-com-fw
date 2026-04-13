// module_b.cpp  — Module B implementation

#include "module_b.h"
#include "app_msg_table.h"
#include "msg_sensor_data.h"

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static ModuleB s_instance;

ModuleB *module_b_instance(void) { return &s_instance; }

// ---------------------------------------------------------------------------
// Lifecycle — Phase 2
// ---------------------------------------------------------------------------

void ModuleB::init()
{
    log("init");
    subscribe(MsgSensorData::ID);
}

// ---------------------------------------------------------------------------
// Runtime message handler
// ---------------------------------------------------------------------------

void ModuleB::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {

        case MsgSensorData::ID: {
            const auto p = MsgSensorData::deserialize(msg);
            log("MsgSensorData  #%lu  temp=%.1f\xc2\xb0""C  ts=%lums",
                (unsigned long)p.counter,
                p.temperature,
                (unsigned long)msg.timestamp);
            break;
        }

        default:
            log_error("unhandled msg_id=0x%04X", msg.msg_id);
            break;
    }
}
