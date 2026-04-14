
#include "app_hw.h"

#include "pal_logger.h"
#include "hsys_task.h"

#include "board.h"

#define __TAG__  "APP_HW  "

static bool _is_initialized = false;
static fp_app_hw_on_event_t _on_event;

static fp_wake_task_t _wake;
static void * _wake_context;

void app_hw_init(const app_hw_init_t * p_hw_init)
{
    if(p_hw_init->fp_app_hw_on_event == NULL)
    {
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_wifi_init: fp_app_wifi_on_event is NULL");
        while(1);
    }
    _on_event = p_hw_init->fp_app_hw_on_event;

    if(p_hw_init->app_init.event_table == NULL)
    {
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_wifi_init: event_table is NULL");
        while(1);
    }

    if(NULL == p_hw_init->app_init.fp_wake || NULL == p_hw_init->app_init.wake_context)
    {
        LOG_MSG_ERROR(LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    _wake = p_hw_init->app_init.fp_wake;
    _wake_context = p_hw_init->app_init.wake_context;    

    LOG_MSG_DEBUG(LOG_EN, "app_hw_init : initialized");

    _is_initialized = true;

    /* Should be started, otherwise state machine will not start */
    if(_wake)
    {
        LOG_MSG_DEBUG(LOG_EN, "Wake From Hsys Iteself");
        _wake(_wake_context);
    }
}

void app_hw_run()
{   
    if(!_is_initialized)
    {
        return;
    }
    
    // LOG_MSG_DEBUG(LOG_EN, "Starting Board Process...");

    board_process(nullptr);

    if(_wake)
    {
        // LOG_MSG_DEBUG(LOG_EN, "Wake From Hsys Iteself");
        _wake(_wake_context);
    }
}
