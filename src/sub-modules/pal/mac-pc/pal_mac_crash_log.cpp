// pal_mac_crash_log.cpp
//
// macOS / simulator stub for pal_crash_log.
// The simulator process does not panic the way an ESP32 does, so all functions
// are no-ops or safe defaults.  pal_crash_log_pending() always returns false so
// the crash-reporting path in ModuleCubeSphere is never exercised on simulator.

#include "pal_crash_log.h"

extern "C" void        pal_crash_log_init(void)                   {}
extern "C" void        pal_crash_log_tick(uint32_t, uint32_t)     {}
extern "C" bool        pal_crash_log_pending(void)                { return false; }
extern "C" void        pal_crash_log_get(pal_crash_info_t *out)   { if (out) *out = {}; }
extern "C" const char *pal_crash_log_get_backtrace(void)          { return ""; }
extern "C" void        pal_crash_log_clear(void)                  {}
