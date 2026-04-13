// module_a.cpp  — Module A implementation

#include "module_a.h"
#include "app_msg_table.h"
#include "msg_sensor_data.h"
#include "msg_tick_1000ms.h"

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
    log("pre_init");
}

// ---------------------------------------------------------------------------
// Lifecycle — Phase 2
// ---------------------------------------------------------------------------

void ModuleA::init()
{
    log("init");
    subscribe(MsgTick1000ms::ID);
}

// ---------------------------------------------------------------------------
// Lifecycle — Phase 3
// ---------------------------------------------------------------------------

void ModuleA::post_init()
{
    log("post_init");
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
            log_error("unhandled msg_id=0x%04X", msg.msg_id);
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
        log_error("create_typed<MsgSensorData> failed");
        return;
    }

    hsys_status_t rc = publish(msg);
    if (rc == HSYS_OK) {
        log("published MsgSensorData #%lu  temp=%.1f",
            (unsigned long)p.counter, p.temperature);
    } else {
        log_error("publish failed (%d)", rc);
        hsys_msg_release(msg);
    }
}
