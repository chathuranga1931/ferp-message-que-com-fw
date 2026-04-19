// module_fuel.cpp
//
// Queue-based frame processing using sanki_6_digit_1 state machine.
//
// Frame path:
//   TCP/ISR thread → _distap_frame_cb() → hsys_queue_send(_frame_queue) → wake()
//   Module task    → on_wake() → _process_queues()
//                 → sanki6_process_data/validate/state_machine()
//                 → publish MsgFuelPumped / MsgNozzleState

#include "module_fuel.h"
#include "msg_config_ready.h"
#include "msg_nozzle_state.h"
#include "msg_fuel_pumped.h"
#include "sanki_6_digit_1.h"
#include "app.h"          // app_config_get()
#include "hsys_msg.h"
#include "pal_logger.h"

#define __TAG__     "FUEL    "
#define FUEL_LOG_EN true

#define MLOG(fmt, ...)  LOG_MSG_INFO( FUEL_LOG_EN, fmt, ##__VA_ARGS__)
#define MLOGE(fmt, ...) LOG_MSG_ERROR(FUEL_LOG_EN, fmt, ##__VA_ARGS__)

// ---------------------------------------------------------------------------
// Queue item — what the worker needs per frame
// ---------------------------------------------------------------------------

typedef struct {
    display_type_t     dtype;
    app_display_data_t data;
} fuel_frame_item_t;

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModuleFuel s_instance;
ModuleFuel *ModuleFuel::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// Static frame callback — bridges C-linkage distap callback → module method
// ---------------------------------------------------------------------------

static void _distap_frame_cb(uint8_t nozzle_idx, display_type_t type,
                              const uint8_t *raw_data)
{
    ModuleFuel::instance()->_on_distap_frame(nozzle_idx, type, raw_data);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ModuleFuel::init()
{
    log("init");

    // Initialise per-nozzle frame queues
    for (uint8_t i = 0; i < FUEL_MAX_NOZZLES; i++) {
        if (!hsys_queue_init(&_frame_queue[i], FUEL_FRAME_QUEUE_DEPTH,
                             sizeof(fuel_frame_item_t))) {
            MLOGE("init: hsys_queue_init failed for nozzle %u", (unsigned)i);
        }
    }

    subscribe(MsgConfigReady::ID);
}

// ---------------------------------------------------------------------------
// Message handler
// ---------------------------------------------------------------------------

void ModuleFuel::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MsgConfigReady::ID:
            if (!_started) {
                _start();
            }
            break;

        default:
            log_error("unhandled msg_id=0x%04X", msg.msg_id);
            break;
    }
}

// ---------------------------------------------------------------------------
// on_wake — called on this module's task thread after wake() was requested.
// Drains the per-nozzle queues and runs the sanki6 state machine.
// ---------------------------------------------------------------------------

void ModuleFuel::on_wake()
{
    _process_queues();
}

// ---------------------------------------------------------------------------
// _start — read config and kick off the driver
// ---------------------------------------------------------------------------

void ModuleFuel::_start()
{
    const app_config_t *cfg = app_config_get();
    _display_type = (display_type_t)cfg->display_type;

    MLOG("starting  display_type=%d  nozzles=%d",
         (int)_display_type, (int)FUEL_MAX_NOZZLES);

    _driver.start(_display_type, _distap_frame_cb);
    _started = true;
}

// ---------------------------------------------------------------------------
// Distap frame callback (TCP thread in simulator, ISR on real HW)
// Called outside the module task — uses queue + wake() to hand off safely.
// ---------------------------------------------------------------------------

void ModuleFuel::_on_distap_frame(uint8_t nozzle_idx, display_type_t type,
                                   const uint8_t *raw_data)
{
    if (nozzle_idx >= FUEL_MAX_NOZZLES || !raw_data) return;

    const display_data_t &frame = *reinterpret_cast<const display_data_t *>(raw_data);

    // Convert to app_display_data_t and push to queue
    fuel_frame_item_t item;
    item.dtype = type;
    fuel_types_from_frame(&item.data, &frame, type, /*nozzle_up=*/true);

    if (!hsys_queue_send(&_frame_queue[nozzle_idx], &item, 0)) {
        MLOGE("_on_distap_frame: queue full for nozzle %u, frame dropped",
              (unsigned)nozzle_idx);
    }

    // Wake the module task — on_wake() will drain the queue on our thread
    wake();
}

// ---------------------------------------------------------------------------
// _process_queues — drain all nozzle queues and run the sanki6 pipeline
// ---------------------------------------------------------------------------

void ModuleFuel::_process_queues()
{
    for (uint8_t idx = 0; idx < FUEL_MAX_NOZZLES; idx++) {
        fuel_frame_item_t item;
        while (hsys_queue_receive(&_frame_queue[idx], &item, 0)) {

            app_display_data_t data = item.data;

            if (!sanki6_process_data(&data)) {
                MLOG("nozzle[%u] sanki6_process_data rejected frame", (unsigned)idx);
                continue;
            }

            sanki6_data_validate(&data, idx);

            bool pumped = sanki6_process_state_machine(&data, idx);

            if (pumped) {
                nozzle_event_t ev{};
                sanki6_get_event(&ev, idx);

                MLOG("nozzle[%u] PUMPED  vol=%lu  unit=%lu  total=%lu",
                     (unsigned)idx,
                     (unsigned long)ev.volume_lx1000,
                     (unsigned long)ev.unit_pricex100,
                     (unsigned long)(uint32_t)ev.total_pricex100);

                _publish_fuel_pumped(idx, ev);
            }

            nozzle_state_t ns = data.start_stop ? NOZZLE_PUMPING : NOZZLE_IDLE;
            _publish_nozzle_state(idx, ns);
        }
    }
}

// ---------------------------------------------------------------------------
// Publish helpers
// ---------------------------------------------------------------------------

void ModuleFuel::_publish_nozzle_state(uint8_t nozzle_idx, nozzle_state_t state)
{
    MsgNozzleState::Payload p{};
    p.nozzle_idx = nozzle_idx;
    p.state      = state;

    hsys_msg_t *msg = create_typed<MsgNozzleState>(p);
    if (!msg) { log_error("create_typed<MsgNozzleState> failed"); return; }
    publish(msg);
}

void ModuleFuel::_publish_fuel_pumped(uint8_t nozzle_idx, const nozzle_event_t &ev)
{
    MsgFuelPumped::Payload p{};
    p.nozzle_idx      = nozzle_idx;
    p.vol_lx1000      = ev.volume_lx1000;
    p.unit_pricex100  = (uint32_t)ev.unit_pricex100;
    p.total_pricex100 = (uint32_t)ev.total_pricex100;

    hsys_msg_t *msg = create_typed<MsgFuelPumped>(p);
    if (!msg) { log_error("create_typed<MsgFuelPumped> failed"); return; }
    publish(msg);
}
