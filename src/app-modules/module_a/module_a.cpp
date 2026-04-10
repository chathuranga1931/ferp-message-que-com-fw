// module_a.cpp  — Module A implementation

#include "module_a.h"
#include "app_msg_table.h"

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
    subscribe(MSG_TICK_1000MS);
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

        case MSG_TICK_1000MS:
            publish_sensor_data();
            break;

        default:
            printf("[%s] unhandled msg_id=%u\n", name(), msg.msg_id);
            break;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ModuleA::publish_sensor_data()
{
    hsys_msg_t *msg = create_msg(MSG_SENSOR_DATA);
    if (msg == nullptr) {
        printf("[%s] ERROR: create_msg failed\n", name());
        return;
    }

    auto *data = static_cast<module_a_sensor_data_t *>(msg->payload);
    data->counter     = ++m_counter;
    data->temperature = 20.0f + (float)(m_counter % 10);

    hsys_status_t rc = publish(msg);
    if (rc == HSYS_OK) {
        printf("[%s] published MSG_SENSOR_DATA #%lu  temp=%.1f\n",
               name(), (unsigned long)data->counter, data->temperature);
    } else {
        printf("[%s] publish failed (%d)\n", name(), rc);
        // msg was not enqueued to anyone; release it manually
        hsys_msg_release(msg);
    }
}
