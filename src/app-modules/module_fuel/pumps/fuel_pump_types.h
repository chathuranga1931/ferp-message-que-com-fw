// fuel_pump_types.h
//
// Thin re-export / forward-declaration header for display-tap types used
// throughout the module_fuel layer.
//
// All authoritative type definitions live in the ferp-device-firmware
// submodule.  We include them here so module_fuel sources only need one
// include instead of navigating the submodule path.

#pragma once

// In the simulator build esp_err.h must be available before display_types.h
// is pulled in.  The PAL shim (pal/mac-pc/esp_err.h) satisfies this when
// ${PAL_MAC} is on the include path.
#include "display_types.h"   // display_type_t, display_data_t
