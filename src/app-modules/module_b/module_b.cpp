// module_b.cpp  — Module B implementation

#include "module_b.h"
#include "app_msg_table.h"
#include "msg_sensor_data.h"

#include <stdio.h>

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
    printf("[%s] init\n", name());
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

            printf("[%s] MsgSensorData  #%lu  temp=%.1f°C  ts=%lums\n",
                   name(),
                   (unsigned long)p.counter,
                   p.temperature,
                   (unsigned long)msg.timestamp);
            break;
        }

        default:
            printf("[%s] unhandled msg_id=0x%04X\n", name(), msg.msg_id);
            break;
    }
}
