// module_a.cpp  — Module A implementation

#include "module_a.h"
#include "app_msg_table.h"
#include "msg_sensor_data.h"
#include "msg_tick_1000ms.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static ModuleA s_instance;

ModuleA *module_a_instance(void) { return &s_instance; }

// ---------------------------------------------------------------------------
// Lifecycle — Phase 1
// ---------------------------------------------------------------------------

void ModuleA::pre_init()
{
    // Hardware / peripheral setup goes here.
    // For this simulation there is nothing to do.
    printf("[%s] pre_init\n", name());
}

// ---------------------------------------------------------------------------
// Lifecycle — Phase 2
// ---------------------------------------------------------------------------

void ModuleA::init()
{
    printf("[%s] init\n", name());

    // Subscribe to the 1 s heartbeat — this is what drives publishing.
    subscribe(MsgTick1000ms::ID);
}

// ---------------------------------------------------------------------------
// Lifecycle — Phase 3
// ---------------------------------------------------------------------------

void ModuleA::post_init()
{
    // Nothing needed here, but the hook is available for cross-module wiring.
    printf("[%s] post_init\n", name());
}

// ---------------------------------------------------------------------------
// Runtime message handler
// ---------------------------------------------------------------------------

void ModuleA::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {

        case MsgTick1000ms::ID:
            publish_sensor_data();
            break;

        default:
            printf("[%s] unhandled msg_id=0x%04X\n", name(), msg.msg_id);
            break;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ModuleA::publish_sensor_data()
{
    MsgSensorData::Payload p{
        .counter     = ++m_counter,
        .temperature = 20.0f + (float)(m_counter % 10),
    };

    hsys_msg_t *msg = create_typed<MsgSensorData>(p);
    if (msg == nullptr) {
        printf("[%s] ERROR: create_typed<MsgSensorData> failed\n", name());
        return;
    }

    hsys_status_t rc = publish(msg);
    if (rc == HSYS_OK) {
        printf("[%s] published MsgSensorData #%lu  temp=%.1f\n",
               name(), (unsigned long)p.counter, p.temperature);
    } else {
        printf("[%s] publish failed (%d)\n", name(), rc);
        hsys_msg_release(msg);
    }
}
