// module_fuel.cpp
//
// Queue-based frame processing using sanki_6_digit_1 state machine.
//
// Frame path:
//   TCP/ISR thread -> _distap_frame_cb() -> hsys_queue_send(_frame_queue) -> wake()
//   Module task    -> on_wake() → _process_queues()
//                 → sanki6_process_data/validate/state_machine()
//                 → publish MsgFuelPumped / MsgNozzleState

#include "module_fuel.h"
#include "msg_config_ready.h"
#include "msg_config_updated.h"
#include "msg_nozzle_state.h"
#include "msg_fuel_pumped.h"
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
#include "msg_timer_alarm.h"
#include "sanki_6_digit_1.h"
#include "censtar_6_digit_1.h"
#include "censtar_7_digit_1.h"
#include "wayne_6_digit_1.h"
#include "hongyang_8_digit_1.h"
#include "longfeng_8_digit_1.h"
#include "app.h"          // app_config_get() — kept for other potential uses
#include "msg_config_ready.h"
#include "msg_config_get_dt.h"
#include "msg_config_dt.h"
#include "msg_dev_info_write.h"
#include "app_device_info.h"
#include "app_config.h"
#include "hsys_msg.h"
#include "pal_logger.h"
#include "pal_gpio.h"
#include "pal_time.h"
#include "app_hw_config.h"
#include <string.h>

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
// Nozzle toggle-button instances (file-scope — module is a singleton)
// These live here rather than as class members so that module_fuel.h does not
// need to expose hsys_tog_button_t to every translation unit that includes it.
// ---------------------------------------------------------------------------
#include "hsys_tog_button.h"
static hsys_tog_button_t s_nozzle_btn[FUEL_MAX_NOZZLES];

// ---------------------------------------------------------------------------
// Static nozzle GPIO ISRs — read pin level → drive toggle-button state machine
// ---------------------------------------------------------------------------

static void _nozzle1_gpio_isr(int32_t /*gpio_num*/, void *arg)
{
    pal_gpio_level_t lvl = PAL_GPIO_LEVEL_LOW;
    pal_gpio_get_level(32, &lvl);
    uint64_t ts = 0;
    if (lvl == PAL_GPIO_LEVEL_HIGH)
        hsys_tog_button_release_event(arg, ts);
    else
        hsys_tog_button_press_event(arg, ts);
}

static void _nozzle2_gpio_isr(int32_t /*gpio_num*/, void *arg)
{
    pal_gpio_level_t lvl = PAL_GPIO_LEVEL_LOW;
    pal_gpio_get_level(33, &lvl);
    uint64_t ts = 0;
    if (lvl == PAL_GPIO_LEVEL_HIGH)
        hsys_tog_button_release_event(arg, ts);
    else
        hsys_tog_button_press_event(arg, ts);
}

// ---------------------------------------------------------------------------
// Static nozzle toggle-button shims — bridge debounced callbacks → module
// ---------------------------------------------------------------------------

// Nozzle 1 (index 0)
static void _nozzle1_up_cb()   { ModuleFuel::instance()->_on_nozzle_up(0);   }
static void _nozzle1_down_cb() { ModuleFuel::instance()->_on_nozzle_down(0); }

// Nozzle 2 (index 1)
static void _nozzle2_up_cb()   { ModuleFuel::instance()->_on_nozzle_up(1);   }
static void _nozzle2_down_cb() { ModuleFuel::instance()->_on_nozzle_down(1); }

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

    // ── Nozzle toggle buttons ────────────────────────────────────────────────
    // Mirror old app_fuel.cpp: debounce 500 ms press / 500 ms release.
    // on_press  → nozzle inserted (DOWN), on_release → nozzle lifted (UP).
    //
    // GPIO assignments (matches board_2602_wrap.h / mac_driver.cpp SIM_NOZZLE_INPUT):
    //   Nozzle 1 → GPIO 32 (INPUT3)
    //   Nozzle 2 → GPIO 33 (INPUT4)

    hsys_tog_button_init(&s_nozzle_btn[0], _nozzle1_down_cb, _nozzle1_up_cb, 500, 500);
    hsys_tog_button_init(&s_nozzle_btn[1], _nozzle2_down_cb, _nozzle2_up_cb, 500, 500);

    pal_gpio_config_t nozzle_cfg{};
    nozzle_cfg.dir           = PAL_GPIO_DIR_INPUT;
    nozzle_cfg.pull          = PAL_GPIO_PULL_UP;
    nozzle_cfg.intr_type     = PAL_GPIO_INTR_ANYEDGE;
    nozzle_cfg.isr_callback  = _nozzle1_gpio_isr;
    nozzle_cfg.isr_arg       = &s_nozzle_btn[0];
    pal_gpio_config(NOZZLE1_GPIO, nozzle_cfg);   // NOZZLE1_GPIO_PIN = INPUT3

    nozzle_cfg.isr_callback  = _nozzle2_gpio_isr;
    nozzle_cfg.isr_arg       = &s_nozzle_btn[1];
    pal_gpio_config(NOZZLE2_GPIO, nozzle_cfg);   // NOZZLE2_GPIO_PIN = INPUT4

    MLOG("init: nozzle GPIOs armed — GPIO32 (nozzle1) GPIO33 (nozzle2)");

    subscribe(MsgConfigReady::ID);
    subscribe(MsgConfigUpdated::ID);
    subscribe(MsgConfigDT::ID);      // DIRECT response from ModuleConfig
    subscribe(MsgTimerAlarm::ID);
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
                // Request device/hardware config from ModuleConfig
                MsgConfigGetDT::Payload req{};
                req.source_module_id = id();
                hsys_msg_t *req_msg = MsgConfigGetDT::create(id(), req);
                if (req_msg) publish(req_msg);
            }
            // Load log rate from config (applies at boot and after live changes)
            {
                const app_config_t *cfg = app_config_get();
                if (cfg) _dt_log_rate = cfg->dt_log_rate;
            }
            break;

        case MsgConfigUpdated::ID: {
            uint16_t key = MsgConfigUpdated::get_key(msg);
            if (key == CFG_KEY_DT_LOG_RATE) {
                const app_config_t *cfg = app_config_get();
                if (cfg) {
                    _dt_log_rate = cfg->dt_log_rate;
                    MLOG("dt_log_rate updated: %lu", (unsigned long)_dt_log_rate);
                }
            }
            break;
        }

        case MsgConfigDT::ID:
            if (!_started) {
                _start(msg);
            }
            break;

        case MsgTimerAlarm::ID:
            // 1 s watchdog fired — run the state machine so stalled nozzles
            // are detected even when no new frames arrive.
            wake();
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
// Nozzle UP / DOWN — called on the debounce timer thread, update cached state
// and wake the module so _process_queues() can see the latest nozzle_state.
// ---------------------------------------------------------------------------

void ModuleFuel::_on_nozzle_up(uint8_t nozzle_idx)
{
    if (nozzle_idx >= FUEL_MAX_NOZZLES) return;
    _nozzle_state[nozzle_idx] = true;
    MLOG("nozzle[%u] UP", (unsigned)nozzle_idx);
    wake();
}

void ModuleFuel::_on_nozzle_down(uint8_t nozzle_idx)
{
    if (nozzle_idx >= FUEL_MAX_NOZZLES) return;
    _nozzle_state[nozzle_idx] = false;
    MLOG("nozzle[%u] DOWN", (unsigned)nozzle_idx);
    wake();
}

// ---------------------------------------------------------------------------
// _start — read config and kick off the driver
// ---------------------------------------------------------------------------

void ModuleFuel::_start(const hsys_msg_t &cfg_msg)
{
    auto p = MsgConfigDT::deserialize(cfg_msg);
    _display_type = (display_type_t)p.display_type;

    // Cache nozzle string IDs for NE_ID generation
    for (uint8_t i = 0; i < FUEL_MAX_NOZZLES; i++) {
        strncpy(_nozzle_str_id[i], p.nozzle_id[i], sizeof(_nozzle_str_id[i]) - 1);
        _nozzle_str_id[i][sizeof(_nozzle_str_id[i]) - 1] = '\0';
    }

    MLOG("DT config received:");
    MLOG("  display_type       = %u", (unsigned)p.display_type);
    MLOG("  stabilize_delay_ms = %u", (unsigned)p.stabilize_delay_ms);
    MLOG("  printer_url        = \"%s\"", p.printer_url);
    MLOG("  printer_copy_count = %u", (unsigned)p.printer_copy_count);
    MLOG("  nozzle_id[0]       = \"%s\"", _nozzle_str_id[0]);
    MLOG("  nozzle_id[1]       = \"%s\"", _nozzle_str_id[1]);

    MLOG("starting  display_type=%d  nozzles=%d",
         (int)_display_type, (int)FUEL_MAX_NOZZLES);

    char dt_version[24] = {};
    _driver.start(_display_type, _distap_frame_cb, dt_version, sizeof(dt_version));
    _started = true;

    // Publish the DT board firmware version to the device-info registry so
    // it appears in the web UI and is accessible to other modules.
    if (dt_version[0] != '\0') {
        hsys_msg_t *w = MsgDevInfoWrite::create_str(
            id(), DEV_INFO_KEY_DISP_TAP_VERSION, dt_version);
        if (w) publish(w);
    }
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

    // Nozzle state is encoded in flags.bits.start_stop by the emulator
    // (same bit that the hardware GPIO toggle buttons set on real hardware).
    bool nozzle_up = (bool)frame.flags.bits.start_stop;
    // _nozzle_state[nozzle_idx] = nozzle_up;

    // MLOG("DISP%d event: type=%d nozzle=%s Vol=%.03f Unit=%.02f Tot=%.02f",
    //      (unsigned)nozzle_idx + 1, (int)type,
    //      nozzle_up ? "UP" : "DOWN",
    //      frame.volume_l/1000.0, frame.unit_price/100.0, frame.total_price/100.0);

    // Convert to app_display_data_t and push to queue
    fuel_frame_item_t item;
    item.dtype = type;
    fuel_types_from_frame(&item.data, &frame, type, nozzle_up);

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


const pump_driver_t pump_drivers[DIS_SIZE] = {
    /* DIS_NONE             [0] */ { nullptr, nullptr, nullptr, nullptr },
    /* DIS_CENSTAR_6_DIGIT  [1] */ {
        (fp_pump_get_event_t *)censtar6_get_event,
        (fp_pump_process_data_t *)censtar6_process_data,
        (fp_pump_process_state_machine_t *)censtar6_process_state_machine,
        (fp_pump_data_validate *)censtar6_data_validate
    },
    /* DIS_CENSTAR_7_DIGIT  [2] */ {
        (fp_pump_get_event_t *)censtar7_get_event,
        (fp_pump_process_data_t *)censtar7_process_data,
        (fp_pump_process_state_machine_t *)censtar7_process_state_machine,
        (fp_pump_data_validate *)censtar7_data_validate
    },
    /* DIS_CENSTAR_7_DIGIT_CS [3] */ {
        (fp_pump_get_event_t *)censtar7_get_event,
        (fp_pump_process_data_t *)censtar7_process_data,
        (fp_pump_process_state_machine_t *)censtar7_process_state_machine,
        (fp_pump_data_validate *)censtar7_data_validate
    },
    /* DIS_HONGYANG_8_DIGIT [4] */ {
        (fp_pump_get_event_t *)hongyang8_get_event,
        (fp_pump_process_data_t *)hongyang8_process_data,
        (fp_pump_process_state_machine_t *)hongyang8_process_state_machine,
        (fp_pump_data_validate *)hongyang8_data_validate
    },
    /* DIS_WAYNE_6_DIGIT    [5] */ {
        (fp_pump_get_event_t *)wayne6_get_event,
        (fp_pump_process_data_t *)wayne6_process_data,
        (fp_pump_process_state_machine_t *)wayne6_process_state_machine,
        (fp_pump_data_validate *)wayne6_data_validate
    },
    /* DIS_SANKI_6_DIGIT    [6] */ {
        (fp_pump_get_event_t *)sanki6_get_event,
        (fp_pump_process_data_t *)sanki6_process_data,
        (fp_pump_process_state_machine_t *)sanki6_process_state_machine,
        (fp_pump_data_validate *)sanki6_data_validate
    },
    /* DIS_LONGFENG_8_DIGIT [7] */ {
        (fp_pump_get_event_t *)longfeng8_get_event,
        (fp_pump_process_data_t *)longfeng8_process_data,
        (fp_pump_process_state_machine_t *)longfeng8_process_state_machine,
        (fp_pump_data_validate *)longfeng8_data_validate
    },
};

void ModuleFuel::_process_queues()
{
    _stop_tick_timer();
    for (uint8_t idx = 0; idx < FUEL_MAX_NOZZLES; idx++) 
    {
        static fuel_frame_item_t item[NO_NOZZELS];
        static app_display_data_t display_data[NO_NOZZELS] = {0}; 
        static unsigned long ts_last_data_received[NO_NOZZELS] =  {0};
        bool is_received = false;

        do 
        {
            is_received = hsys_queue_receive(&_frame_queue[idx], &item[idx], 0);
            if(is_received) 
            { 
                ts_last_data_received[idx] = pal_time_get_ms(); 

                display_data[idx].total_pricex100 = (uint64_t)item[idx].data.total_pricex100;
                display_data[idx].volume_lx1000 = (uint64_t)item[idx].data.volume_lx1000;
                display_data[idx].unit_pricex100 = (uint64_t)item[idx].data.unit_pricex100;
                display_data[idx].fuel_type = item[idx].dtype;
                display_data[idx].start_stop = item[idx].data.start_stop;  // Ensure latest nozzle state from frame       
                display_data[idx].select_ll = item[idx].data.select_ll;     
            }

            display_type_t dtype = (display_type_t)display_data[idx].fuel_type;
            switch(dtype)
            {
                case DIS_NONE:
                    // MLOG("DIS_NONE");
                break;
                case DIS_LONGFENG_8_DIGIT:
                case DIS_CENSTAR_6_DIGIT:
                case DIS_CENSTAR_7_DIGIT_CS:
                case DIS_CENSTAR_7_DIGIT:
                case DIS_WAYNE_6_DIGIT:
                case DIS_SANKI_6_DIGIT:
                {
                    display_data[idx].start_stop = _nozzle_state[idx];  
                    // MLOG("CEN/SAN/LONG %s", display_data[idx].start_stop ? "UP" : "DN");
                }
                break;
                case DIS_HONGYANG_8_DIGIT:
                break;
                default:
                    MLOG("UNKNOWN DISPLAY TYPE");
                break;
            }

            if(is_received)
            {
                // Rate limiting: dt_log_rate is frames-per-minute.
                // 0 or >= 500 → log every frame.
                // 1–499      → enforce a minimum gap of (60000/rate) ms
                //               between consecutive log lines.
                bool do_log = true;
                if (_dt_log_rate > 0 && _dt_log_rate < 500)
                {
                    uint64_t now_ms  = pal_time_get_ms();
                    uint64_t gap_ms  = 60000ULL / _dt_log_rate;
                    do_log = (now_ms - _last_log_ms >= gap_ms);
                    if (do_log) _last_log_ms = now_ms;
                }

                if (do_log) 
                {
                    static char log_str1[64];
                    static char log_str2[64];

                    if(idx == 0) 
                    {
                        sprintf(log_str1, "[%1d] %2d %7.03f %7.02f %8.02f %s",
                            (unsigned)idx, (int)item[idx].dtype,
                            display_data[idx].volume_lx1000/1000.0, display_data[idx].unit_pricex100/100.0, display_data[idx].total_pricex100/100.0,
                            display_data[idx].start_stop ? "UP" : "DN"); 
                    }
                    else if(idx == 1)
                    {
                        sprintf(log_str2, "[%1d] %2d %7.03f %7.02f %8.02f %s",
                            (unsigned)idx, (int)item[idx].dtype,
                            display_data[idx].volume_lx1000/1000.0, display_data[idx].unit_pricex100/100.0, display_data[idx].total_pricex100/100.0,
                            display_data[idx].start_stop ? "UP" : "DN"); 
                    }   
                    else
                    {
                    } 
                    MLOG("Received frame: %s  %s", log_str1, log_str2);
                }
            }

            if(pump_drivers[dtype].process_data == nullptr || 
               pump_drivers[dtype].process_state_machine == nullptr || 
               pump_drivers[dtype].get_event == nullptr || 
               pump_drivers[dtype].data_validate == nullptr )
            {
                continue;
            }

            if(display_data[idx].volume_lx1000 == 0 && display_data[idx].unit_pricex100 == 0 && display_data[idx].total_pricex100 == 0)
            {
                // If all values are zero, it's likely a "nozzle down" frame with no valid data. Skip processing to avoid false events.
                continue;
            }

            // disable handling totalizer for now. TODO
            bool is_totalizer = display_data[idx].select_ll;
            if(is_totalizer)
            {
                // MLOG("Totalizer frame detected for nozzle[%u], skipping event generation", (unsigned)idx);
                continue;
            }

            bool is_valid = pump_drivers[dtype].process_data(&display_data[idx]);
            if (!is_valid) 
            {
                MLOG("nozzle[%u] process_data rejected frame", (unsigned)idx);
                continue;
            }


            pump_drivers[dtype].data_validate(&display_data[idx], idx);                

            static pumping_state_t state_prev[FUEL_MAX_NOZZLES];
            pumping_state_t state;
            bool pumped = pump_drivers[dtype].process_state_machine(&display_data[idx], idx, &state);

            if (pumped) 
            {
                nozzle_event_t ev{};
                pump_drivers[dtype].get_event(&ev, idx);

                MLOG("nozzle[%u] PUMPED  vol=%lu  unit=%lu  total=%lu",
                    (unsigned)idx,
                    (unsigned long)ev.volume_lx1000,
                    (unsigned long)ev.unit_pricex100,
                    (unsigned long)(uint32_t)ev.total_pricex100);

                _publish_fuel_pumped(idx, ev);
            }

            if(state_prev[idx] != state)
            {
                state_prev[idx] = state;
                if(state == Pumping_State_Unknown)
                {
                    nozzle_state_t ns = NOZZLE_IDLE;
                    _publish_nozzle_state(idx, ns);
                    MLOG("nozzle[%u] state= IDLE", (unsigned)idx);
                }
                else if(state == Pumping_State_Stopped)
                {
                    nozzle_state_t ns = NOZZLE_PUMPED;
                    _publish_nozzle_state(idx, ns);
                    MLOG("nozzle[%u] state= PUMPED", (unsigned)idx);
                }
                else 
                {
                    nozzle_state_t ns = NOZZLE_PUMPING;
                    _publish_nozzle_state(idx, ns);
                    MLOG("nozzle[%u] state= PUMPING", (unsigned)idx);
                }
            }

        }while(is_received);
    }

    _start_tick_timer();
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
    time_t epoch = 0;
    pal_time_get_epoch_time(&epoch);
    uint32_t ts = (uint32_t)epoch;

    MsgFuelPumped::Payload p{};
    p.nozzle_idx      = nozzle_idx;
    p.vol_lx1000      = ev.volume_lx1000;
    p.unit_pricex100  = (uint32_t)ev.unit_pricex100;
    p.total_pricex100 = (uint32_t)ev.total_pricex100;
    p.time_stamp      = ts;
    p.ne_id           = _compute_ne_id(nozzle_idx, ts);

    MLOG("nozzle[%u] NE_ID=%llu  ts=%lu", (unsigned)nozzle_idx,
         (unsigned long long)p.ne_id, (unsigned long)ts);

    hsys_msg_t *msg = create_typed<MsgFuelPumped>(p);
    if (!msg) { log_error("create_typed<MsgFuelPumped> failed"); return; }
    publish(msg);
}

// ---------------------------------------------------------------------------
// _compute_ne_id — port of legacy get_unique_event_id() from ferp_client.cpp
//
// Nozzle ID table maps string IDs to fuel type codes.
// The algorithm encodes both the compressed timestamp and the fuel type code
// into a single uint64 unique event ID.
// ---------------------------------------------------------------------------

static const struct { char id[5]; uint8_t code; } k_nozzle_id_table[] = {
    {"P01", 11}, {"P02", 12}, {"P03", 13}, {"P04", 14}, {"P05", 15},
    {"P06", 16}, {"P07", 17}, {"P08", 18}, {"P09", 19}, {"P10", 11},
    {"D01", 21}, {"D02", 22}, {"D03", 23}, {"D04", 24}, {"D05", 25},
    {"D06", 26}, {"D07", 27}, {"D08", 28}, {"D09", 29}, {"D10", 30},
    {"SP01",41}, {"SP02",42}, {"SP03",43}, {"SP04",44}, {"SP05",45},
    {"SD01",51}, {"SD02",52}, {"SD03",53}, {"SD04",54}, {"SD05",55},
    {"K01", 91}, {"K02", 92}, {"K03", 93}, {"K04", 94}, {"K05", 95},
};
static const uint8_t k_nozzle_id_table_size =
    (uint8_t)(sizeof(k_nozzle_id_table) / sizeof(k_nozzle_id_table[0]));

uint64_t ModuleFuel::_compute_ne_id(uint8_t nozzle_idx, uint32_t timestamp) const
{
    // Compress timestamp: subtract epoch base (Dec 18 2024 00:00:00 UTC)
    static const uint32_t k_epoch_base = 1734480000UL;
    uint64_t ts = (timestamp > k_epoch_base) ? (uint64_t)(timestamp - k_epoch_base) : 0ULL;

    uint8_t last_two = (uint8_t)(ts % 100);
    ts = (ts / 100) * 10000 + last_two;

    // Look up fuel type code from nozzle string ID
    uint8_t fuel_code = 99;  // default if not found
    const char *nozzle_str = (nozzle_idx < FUEL_MAX_NOZZLES) ? _nozzle_str_id[nozzle_idx] : "";
    for (uint8_t i = 0; i < k_nozzle_id_table_size; i++) {
        if (strncmp(nozzle_str, k_nozzle_id_table[i].id, sizeof(k_nozzle_id_table[i].id)) == 0) {
            fuel_code = k_nozzle_id_table[i].code;
            break;
        }
    }

    return ts + ((uint64_t)fuel_code * 100);
}

// ---------------------------------------------------------------------------

void ModuleFuel::_stop_tick_timer()
{
    // Cancel any existing timer slot.  If none exists ModuleTimer will warn
    // (TIMER_RESULT_ERR_NOT_FOUND) — that is acceptable and expected on the
    // first call.
    MsgTimerStop::Payload sp{};
    sp.source_module_id = id();
    hsys_msg_t *smsg = MsgTimerStop::create(id(), sp);
    if (smsg) {
        // Use a short bounded wait instead of fire-and-forget: dropping the
        // STOP message can leave the watchdog timer running across queue churn.
        (void)send(smsg, MODULE_TIMER_ID, 50);
    }
}

void ModuleFuel::_start_tick_timer()
{
    // Arm a one-shot 1-second timer so the module wakes at least once per
    // second even when no new frames arrive (keeps state machine running).
    MsgTimerStart::Payload tp{};
    tp.source_module_id = id();
    tp.start_offset_ms  = 0;
    tp.duration_ms      = 1000;
    tp.is_repetitive    = false;   // one-shot — re-armed on each on_wake()
    tp.forced           = false;

    hsys_msg_t *tmsg = MsgTimerStart::create(id(), tp);
    if (tmsg) {
        // START is required for the watchdog path, so make it delivery-safe as
        // well.  A short wait is enough for the timing task to drain normally.
        (void)send(tmsg, MODULE_TIMER_ID, 50);
    }
}
