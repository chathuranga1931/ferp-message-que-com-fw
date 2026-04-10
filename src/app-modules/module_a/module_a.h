// module_a.h
//
// Module A — Sensor publisher
//
// Inherits HsysModule.  Overrides:
//   init()          → subscribes to MSG_TICK_1000MS
//   on_msg_received()  → on each tick, publishes MSG_SENSOR_DATA
//
// Task binding: "sensor_task"

#ifndef MODULE_A_H
#define MODULE_A_H

#include "hsys_module.h"

#define MODULE_A_ID   ((hsys_module_id_t)1)
#define MODULE_A_NAME "module_a"

// ---------------------------------------------------------------------------
// Payload published by this module
// ---------------------------------------------------------------------------

typedef struct {
    uint32_t counter;       ///< Increments with every publish
    float    temperature;   ///< Simulated sensor value
} module_a_sensor_data_t;

// ---------------------------------------------------------------------------
// Module class
// ---------------------------------------------------------------------------

class ModuleA : public HsysModule
{
public:
    ModuleA() : HsysModule(MODULE_A_ID, MODULE_A_NAME) {}

protected:
    // Phase 1 — hardware / peripheral setup (none needed for simulation)
    void pre_init()  override;

    // Phase 2 — subscribe to the heartbeat tick
    void init()      override;

    // Phase 3 — nothing needed, but shown as example
    void post_init() override;

    // Runtime — react to MSG_TICK_1000MS, publish MSG_SENSOR_DATA
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    void publish_sensor_data();

    uint32_t m_counter = 0;
};

// ---------------------------------------------------------------------------
// Singleton accessor
// ---------------------------------------------------------------------------

/**
 * @brief  Returns the single firmware-lifetime instance of ModuleA.
 *         Pass the returned pointer to hsys_module_register().
 */
ModuleA *module_a_instance(void);

#endif // MODULE_A_H
