// module_b.cpp  — Module B implementation

#include "module_b.h"
#include "app_msg_table.h"
#include "module_a.h"   // for module_a_sensor_data_t

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
    subscribe(MSG_SENSOR_DATA);
}

// ---------------------------------------------------------------------------
// Runtime message handler
// ---------------------------------------------------------------------------

void ModuleB::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {

        case MSG_SENSOR_DATA: {
            if (msg.payload == nullptr ||
                msg.payload_size < sizeof(module_a_sensor_data_t)) {
                printf("[%s] ERROR: bad payload\n", name());
                break;
            }
            const auto *data =
                static_cast<const module_a_sensor_data_t *>(msg.payload);

            printf("[%s] MSG_SENSOR_DATA  #%lu  temp=%.1f°C  ts=%lums\n",
                   name(),
                   (unsigned long)data->counter,
                   data->temperature,
                   (unsigned long)msg.timestamp);
            // No pool_free — hsys_msg_release() is called automatically
            // by the dispatch loop after on_msg_received() returns.
            break;
        }

        default:
            printf("[%s] unhandled msg_id=%u\n", name(), msg.msg_id);
            break;
    }
}
