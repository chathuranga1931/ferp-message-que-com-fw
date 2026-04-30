#include "app_ota.h"
#include "app_internet.h"

#include "pal_power.h"
#include "pal_logger.h"

#include "hsys_soft_timer.h"
#include "hsys_event.h"

#include <string.h>

#define __TAG__ "APP_OTA "

#define OTA_EVENT_INTERNET_CONNECTED    (0x1 << 0)
#define OTA_EVENT_TIMER_FIRED           (0x1 << 1)

#define OTA_CHECK_PERIOD_MS     (300UL * 1000UL)

#define OTA_DEBUG_LOG_EN      LOG_DIS
#define OTA_WARN_LOG_EN       LOG_DIS
#define OTA_ERROR_LOG_EN      LOG_DIS
#define OTA_INFO_LOG_EN       LOG_DIS

/* -------------------------------------------------------------------------
 * Per-driver state machine
 * ------------------------------------------------------------------------- */
typedef enum {
    OTA_STATE_WAIT_FOR_INTERNET,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOAD_PENDING,
    OTA_STATE_DOWNLOADING,
} app_ota_state_t;

typedef struct {
    hsys_ota_driver_t*  drv;
    app_ota_state_t     state;
    char                pending_version[PAL_OTA_MAX_VERSION_LEN];
    uint8_t             my_idx;
    bool is_ready;
} _ota_drv_ctx_t;

/* -------------------------------------------------------------------------
 * Module globals — shared across all drivers
 * ------------------------------------------------------------------------- */
#define APP_OTA_MAX_DRIVERS  8

static bool _is_initialized = false;

static _ota_drv_ctx_t _drv_ctxs[APP_OTA_MAX_DRIVERS];
static uint8_t        _drv_count = 0;

static hsys_eventgroup_handle_t _ota_events;
static hsys_timer_handle_t      _periodic_timer;

static fp_wake_task_t _wake        = NULL;
static void*          _wake_ctx    = NULL;

static bool _is_internet_connected = false;

static fp_app_ota_on_event_t _on_event;

/* -------------------------------------------------------------------------
 * Timer callback — fires every OTA_CHECK_PERIOD_MS
 * ------------------------------------------------------------------------- */
static void _timer_callback(void* arg)
{
    hsys_event_group_set_bits(_ota_events, OTA_EVENT_TIMER_FIRED);

    if (_wake) {
        LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: wake from timer");
        _wake(_wake_ctx);
    }
}

/* -------------------------------------------------------------------------
 * Internet event handler
 *
 * Updates _is_internet_connected and wakes the task.
 * OTA_EVENT_INTERNET_CONNECTED is used only as a one-shot trigger to wake
 * the task on the first connect; _is_internet_connected is the authoritative
 * connectivity flag checked inside app_ota_run().
 * ------------------------------------------------------------------------- */
static void _on_internet_event(app_internet_event_t event, void* arg)
{
    switch (event)
    {
        case APP_INTERNET_EVENT_CONNECTED:
            if (!_is_internet_connected)
            {
                _is_internet_connected = true;
                hsys_event_group_set_bits(_ota_events, OTA_EVENT_INTERNET_CONNECTED);
                LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: internet connected");
            }
            break;

        case APP_INTERNET_EVENT_DISCONNECTED:
            if (_is_internet_connected)
            {
                _is_internet_connected = false;
                hsys_event_group_set_bits(_ota_events, OTA_EVENT_INTERNET_CONNECTED);
                LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: internet disconnected");
            }
            break;

        default:
            break;
    }

    if (_wake) {
        _wake(_wake_ctx);
    }
}

/* -------------------------------------------------------------------------
 * Per-driver helpers
 * ------------------------------------------------------------------------- */
static app_ota_state_t _do_check(_ota_drv_ctx_t* ctx)
{
    hsys_ota_driver_t* drv = ctx->drv;

    if (!drv->fp_check_version)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA[%s]: fp_check_version not set",
                      drv->fp_get_firmware_type ? drv->fp_get_firmware_type() : "?");
        return OTA_STATE_CHECKING;
    }

    hsys_ota_result_t result = {};
    int32_t ret = drv->fp_check_version(drv, &result);

    if (ret != PAL_OK)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA[%s]: version check failed",
                      drv->fp_get_firmware_type ? drv->fp_get_firmware_type() : "?");
        return OTA_STATE_CHECKING;
    }

    if (!result.update_available)
    {
        LOG_MSG_INFO(OTA_DEBUG_LOG_EN, "OTA[%s]: firmware up to date (%s)",
                     drv->fp_get_firmware_type   ? drv->fp_get_firmware_type()   : "?",
                     drv->fp_get_current_version ? drv->fp_get_current_version() : "?");
        return OTA_STATE_CHECKING;
    }

    strncpy(ctx->pending_version, result.latest_version, sizeof(ctx->pending_version) - 1);
    ctx->pending_version[sizeof(ctx->pending_version) - 1] = '\0';

    LOG_MSG_INFO(OTA_DEBUG_LOG_EN, "OTA[%s]: update available %s → %s  (%lu bytes  CRC=0x%08lX)",
                 drv->fp_get_firmware_type   ? drv->fp_get_firmware_type()   : "?",
                 drv->fp_get_current_version ? drv->fp_get_current_version() : "?",
                 ctx->pending_version,
                 (unsigned long)result.file_size,
                 (unsigned long)result.crc32);

    return OTA_STATE_DOWNLOAD_PENDING;
}

static app_ota_state_t _do_download(_ota_drv_ctx_t* ctx)
{
    hsys_ota_driver_t* drv = ctx->drv;

    if (!drv->fp_download_and_flash)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA[%s]: fp_download_and_flash not set",
                      drv->fp_get_firmware_type ? drv->fp_get_firmware_type() : "?");
        return OTA_STATE_WAIT_FOR_INTERNET;
    }

    int32_t ret = drv->fp_download_and_flash(drv, ctx->pending_version);

    if (ret != PAL_OK)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA[%s]: download/flash failed — will retry on next timer tick",
                      drv->fp_get_firmware_type ? drv->fp_get_firmware_type() : "?");
        return OTA_STATE_WAIT_FOR_INTERNET;
    }

    LOG_MSG_INFO(OTA_DEBUG_LOG_EN, "OTA[%s]: firmware flashed successfully — rebooting",
                 drv->fp_get_firmware_type ? drv->fp_get_firmware_type() : "?");

    _on_event(APP_OTA_EVENT_DOWNLOAD_SUCCESS, (void*)ctx->my_idx);

    /* pal_power_reset() does not return */
    return OTA_STATE_WAIT_FOR_INTERNET;
}

/* -------------------------------------------------------------------------
 * Per-driver state machine tick — called once per timer fire per driver
 * Returns true if any driver triggered a reboot (download succeeded)
 * ------------------------------------------------------------------------- */
static void _tick_driver(_ota_drv_ctx_t* ctx)
{
    LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA[%s]: tick  state=%d",
                  ctx->drv->fp_get_firmware_type ? ctx->drv->fp_get_firmware_type() : "?",
                  (int)ctx->state);
    switch (ctx->state)
    {
        case OTA_STATE_WAIT_FOR_INTERNET:
            /* Transition is driven at module level when internet connects */
            _is_internet_connected? (ctx->state = OTA_STATE_CHECKING) : (ctx->state = OTA_STATE_WAIT_FOR_INTERNET);

            break;

        case OTA_STATE_CHECKING:
            ctx->state = _do_check(ctx);
            if(ctx->state == OTA_STATE_DOWNLOAD_PENDING) {
                if (_wake) {
                    hsys_event_group_set_bits(_ota_events, OTA_EVENT_TIMER_FIRED);
                    _wake(_wake_ctx);
                }
            }
            break;

        case OTA_STATE_DOWNLOAD_PENDING:
            /* Immediately proceed — add a confirmation gate here if needed */
            ctx->state = OTA_STATE_DOWNLOADING;

            if(ctx->state == OTA_STATE_DOWNLOADING) {
                if (_wake) {
                    hsys_event_group_set_bits(_ota_events, OTA_EVENT_TIMER_FIRED);
                    _wake(_wake_ctx);
                }
            }
            break;

        case OTA_STATE_DOWNLOADING:
            ctx->state = _do_download(ctx);
            break;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

int32_t app_ota_init(const app_ota_init_t* init)
{
    if (!init || !init->drivers || init->driver_count == 0)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA: drivers array is NULL or empty");
        while (1);
    }

    if (init->driver_count > APP_OTA_MAX_DRIVERS)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA: driver_count %u exceeds APP_OTA_MAX_DRIVERS (%d)",
                      init->driver_count, APP_OTA_MAX_DRIVERS);
        while (1);
    }

    if (!init->app_init.fp_wake || !init->app_init.wake_context)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA: fp_wake / wake_context is NULL");
        while (1);
    }

    if (!init->app_init.event_table)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA: event_table is NULL");
        while (1);
    }

    if(!init->fp_app_ota_on_event)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA: fp_app_ota_on_event registered");
        while(1);
    }
    
    _wake      = init->app_init.fp_wake;
    _wake_ctx  = init->app_init.wake_context;
    _drv_count = init->driver_count;
    _on_event   = init->fp_app_ota_on_event;

    for (uint8_t i = 0; i < _drv_count; i++)
    {
        _drv_ctxs[i].drv   = init->drivers[i];
        _drv_ctxs[i].state = OTA_STATE_WAIT_FOR_INTERNET;
        _drv_ctxs[i].my_idx = i;
        memset(_drv_ctxs[i].pending_version, 0, sizeof(_drv_ctxs[i].pending_version));

        LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: driver[%u]  type=%s  ver=%s  server=%s",
                      (unsigned)i,
                      init->drivers[i]->fp_get_firmware_type   ? init->drivers[i]->fp_get_firmware_type()   : "?",
                      init->drivers[i]->fp_get_current_version ? init->drivers[i]->fp_get_current_version() : "?",
                      init->drivers[i]->fp_get_server_url      ? init->drivers[i]->fp_get_server_url()      : "?");
    }

    _ota_events     = hsys_event_group_create();
    _periodic_timer = hsys_timer_create("OTA Timer", OTA_CHECK_PERIOD_MS, true, NULL, _timer_callback);

    /* Register for internet connectivity events */
    init->app_init.event_table->on_internet_event = (fp_event_interface_t)_on_internet_event;

    _is_initialized = true;

    LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: initialized  drivers=%u  period=%lus",
                  (unsigned)_drv_count,
                  (unsigned long)(OTA_CHECK_PERIOD_MS / 1000));

    return 0;
}

int32_t app_ota_trigger_check(void)
{
    hsys_event_group_set_bits(_ota_events, OTA_EVENT_TIMER_FIRED);

    if (_wake)
    {
        _wake(_wake_ctx);
    }

    return 0;
}

int32_t app_ota_on_driver_ready(uint8_t driver_index)
{
    if (driver_index >= _drv_count)
    {
        LOG_MSG_ERROR(OTA_DEBUG_LOG_EN, "OTA: invalid driver_index %u (max %u)",
                      (unsigned)driver_index, (unsigned)(_drv_count - 1));
        return -1;
    }

    _drv_ctxs[driver_index].is_ready = true;
    LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: driver[%u] is ready", (unsigned)driver_index);

    hsys_event_group_set_bits(_ota_events, OTA_EVENT_TIMER_FIRED);
    if (_wake)
    {
        _wake(_wake_ctx);
    }

    return 0;
}

bool _is_network_connected = false;

int32_t app_ota_device_network_connected(void)
{
    _is_network_connected = true;
    hsys_event_group_set_bits(_ota_events, OTA_EVENT_TIMER_FIRED);

    if (_wake)
    {
        LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: wake from device_network_connected");
        _wake(_wake_ctx);
    }

    return 0;
}

int32_t app_ota_run(void)
{
    if (!_is_initialized) return -1;

    /* -----------------------------------------------------------------------
     * Consume the internet-change wake bit (used only to ensure the task
     * runs at least once after a connect/disconnect; _is_internet_connected
     * is already up-to-date when we arrive here).
     * --------------------------------------------------------------------- */
    uint32_t inet_bits = hsys_event_group_wait_bits(_ota_events, OTA_EVENT_INTERNET_CONNECTED, 1, 0, 0);

    if (inet_bits & OTA_EVENT_INTERNET_CONNECTED)
    {
        if (_is_internet_connected)
        {
            /* --- Just connected --- */
            LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: internet ready — starting periodic check timer");
            hsys_start_timer(_periodic_timer);

            for (uint8_t i = 0; i < _drv_count; i++)
            {
                if (_drv_ctxs[i].state == OTA_STATE_WAIT_FOR_INTERNET)
                {
                    _drv_ctxs[i].state = OTA_STATE_CHECKING;
                }
            }

            /* Fire an immediate check — don't wait for the first 30 s tick */
            hsys_event_group_set_bits(_ota_events, OTA_EVENT_TIMER_FIRED);
        }
        else
        {
            /* --- Just disconnected --- */
            LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: internet lost — stopping timer");
            hsys_stop_timer(_periodic_timer);
            hsys_event_group_clear_bits(_ota_events, OTA_EVENT_TIMER_FIRED);

            for (uint8_t i = 0; i < _drv_count; i++)
            {
                _drv_ctxs[i].state = OTA_STATE_WAIT_FOR_INTERNET;
            }
        }
    }

    if (!_is_internet_connected) return 0;
    if( !_is_network_connected) return 0;

    /* -----------------------------------------------------------------------
     * On each timer tick, run one state-machine tick for every driver
     * --------------------------------------------------------------------- */
    uint32_t timer_bits = hsys_event_group_wait_bits(_ota_events, OTA_EVENT_TIMER_FIRED, 1, 0, 0);

    if (!(timer_bits & OTA_EVENT_TIMER_FIRED)) return 0;

    LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: timer fired — ticking %u driver(s)", (unsigned)_drv_count);

    for (uint8_t i = 0; i < _drv_count; i++)
    {
        if(_drv_ctxs[i].is_ready)
        {
            LOG_MSG_DEBUG(OTA_DEBUG_LOG_EN, "OTA: ticking driver[%u]", (unsigned)i);
            _tick_driver(&_drv_ctxs[i]);
        }
    }

    return 0;
}
