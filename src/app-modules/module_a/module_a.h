// module_a.h
//
// Module A — Sensor publisher
//
// Inherits HsysModule.  Overrides:
//   init()             → subscribes to MsgTick1000ms
//   on_msg_received()  → on each tick, publishes MsgSensorData
//
// Task binding: "sensor_task"

#ifndef MODULE_A_H
#define MODULE_A_H

#include "hsys_module.h"

#define MODULE_A_ID   ((hsys_module_id_t)1)
#define MODULE_A_NAME "module_a"

// ---------------------------------------------------------------------------
// Module class
// ---------------------------------------------------------------------------

class ModuleA : public HsysModule
{
public:
    ModuleA() : HsysModule(MODULE_A_ID, MODULE_A_NAME) {}

protected:
    void pre_init()  override;
    void init()      override;
    void post_init() override;
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
