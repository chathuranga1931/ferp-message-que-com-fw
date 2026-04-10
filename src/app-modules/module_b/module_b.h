// module_b.h
//
// Module B — Display / subscriber
//
// Inherits HsysModule.  Overrides:
//   init()          → subscribes to MSG_SENSOR_DATA
//   on_msg_received()  → prints the received sensor payload
//
// Task binding: "display_task"

#ifndef MODULE_B_H
#define MODULE_B_H

#include "hsys_module.h"

#define MODULE_B_ID   ((hsys_module_id_t)2)
#define MODULE_B_NAME "module_b"

class ModuleB : public HsysModule
{
public:
    ModuleB() : HsysModule(MODULE_B_ID, MODULE_B_NAME) {}

protected:
    // Phase 2 — subscribe to sensor data
    void init() override;

    // Runtime — print the received payload
    void on_msg_received(const hsys_msg_t &msg) override;
};

/**
 * @brief  Returns the single firmware-lifetime instance of ModuleB.
 */
ModuleB *module_b_instance(void);

#endif // MODULE_B_H
