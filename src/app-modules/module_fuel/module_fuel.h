// module_fuel.h
//
// ModuleFuel — owns the full fueling subsystem.
//
// Lifecycle:
//   init()            — subscribe to MsgConfigReady, init per-nozzle frame queues
//   on_msg_received() — on MsgConfigReady: read display_type, start driver
//   on_wake()         — drain per-nozzle frame queues → sanki6 pipeline
//
// Frame path (TCP thread / ISR → module task):
//   _distap_frame_cb() → hsys_queue_send(_frame_queue) → wake()
//   Task thread        → on_wake() → _process_queues()
//                      → sanki6_process_data/validate/state_machine()
//                      → publish MsgFuelPumped / MsgNozzleState
//
// Outbound messages:
//   MsgNozzleState  (0x0801)  — on every nozzle state transition
//   MsgFuelPumped   (0x0800)  — on confirmed complete transaction

#pragma once

#include "hsys_module.h"
#include "hsys_queue.h"
#include "fuel_disptap_driver.h"
#include "fuel_config.h"
#include "fuel_types.h"
#include "nozzle_event.h"
#include "display_types.h"
#include "pumping_types.h"

#include "app_module_ids.h"
#define MODULE_FUEL_NAME  "fuel"

// Depth of the per-nozzle frame queue (frames received faster than they are
// processed are buffered here; old frames are discarded if the queue is full).
#define FUEL_FRAME_QUEUE_DEPTH  8

typedef bool fp_pump_get_event_t(nozzle_event_t * ne, uint8_t idx);
typedef bool fp_pump_process_data_t(app_display_data_t * display_data);
typedef bool fp_pump_process_state_machine_t(app_display_data_t * display_data, uint8_t nozzle_id, pumping_state_t * state);
typedef void fp_pump_data_validate(const app_display_data_t * display_data, uint8_t nozzle_id);

typedef struct {
    fp_pump_get_event_t * get_event;
    fp_pump_process_data_t * process_data;
    fp_pump_process_state_machine_t * process_state_machine;
    fp_pump_data_validate * data_validate;
} pump_driver_t;

class ModuleFuel : public HsysModule
{
public:
    ModuleFuel() : HsysModule(MODULE_FUEL_ID, MODULE_FUEL_NAME) {}

    static ModuleFuel *instance();

    // Called from the static C-linkage distap frame callback (TCP thread)
    void _on_distap_frame(uint8_t nozzle_idx, display_type_t type,
                          const uint8_t *raw_data);

    // Called from the static toggle-button shims (board debounce thread / ISR)
    void _on_nozzle_up  (uint8_t nozzle_idx);
    void _on_nozzle_down(uint8_t nozzle_idx);

protected:
    void init()             override;
    void on_msg_received(const hsys_msg_t &msg) override;

    // Called on the module's own task thread whenever wake() was requested.
    // Drains the per-nozzle frame queues and runs the sanki6 state machine.
    void on_wake()          override;

private:
    FuelDispTapDriver     _driver;

    // Per-nozzle frame queues (filled from TCP/ISR thread, drained in on_wake)
    hsys_queue_handle_t   _frame_queue[FUEL_MAX_NOZZLES];

    // Last known nozzle state (set from flags.bits.start_stop in each frame,
    // mirrors the hardware GPIO toggle-button state in the real firmware).
    bool                  _nozzle_state[FUEL_MAX_NOZZLES] = {};

    display_type_t        _display_type = (display_type_t)0;
    bool                  _started = false;
    bool                  _timer_active = false;

    void _start(const hsys_msg_t &cfg_msg);
    void _process_queues();

    void _publish_nozzle_state(uint8_t nozzle_idx, nozzle_state_t state);
    void _publish_fuel_pumped (uint8_t nozzle_idx, const nozzle_event_t &ev);
    void _publish_wake_message(uint32_t duration_ms);
};
