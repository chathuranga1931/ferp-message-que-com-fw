// hsys_task_append.cpp  —  FreeRTOS implementation
//
// Implements the additional OS primitives declared in hsys_task_append.h.
// FreeRTOS-specific. To port: create a sibling file for your RTOS and
// implement the same three functions.

#include "hsys_task_append.h"

extern "C" {
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/portmacro.h"
}

// ---------------------------------------------------------------------------
// Uptime query
// ---------------------------------------------------------------------------

uint32_t hsys_task_get_tick_ms(void)
{
    // xTaskGetTickCount() returns ticks; pdTICKS_TO_MS converts to ms.
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

// ---------------------------------------------------------------------------
// Critical section
// ---------------------------------------------------------------------------

// FreeRTOS requires a per-call state variable when using the
// taskENTER_CRITICAL / taskEXIT_CRITICAL macros on Xtensa (ESP32).
// We keep it as a function-level static to match the expected
// enter/exit pairing pattern used in the pool manager.
//
// NOTE: This is intentionally NOT re-entrant.  The pool manager must
//       never nest critical sections.

static portMUX_TYPE s_critical_mux = portMUX_INITIALIZER_UNLOCKED;

void hsys_critical_enter(void)
{
    taskENTER_CRITICAL(&s_critical_mux);
}

void hsys_critical_exit(void)
{
    taskEXIT_CRITICAL(&s_critical_mux);
}
