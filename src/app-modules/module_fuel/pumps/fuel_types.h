// fuel_types.h
//
// Bridge header: re-exposes display_data_t (from the DT board library) as the
// app_display_data_t type expected by sanki_6_digit_1.cpp.
//
// The DT board library and the old-app used different field names for the same
// physical values.  Rather than editing sanki_6_digit_1.cpp we define
// app_display_data_t here using the same layout so the file compiles unchanged.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "display_types.h"   // display_type_t, display_data_t, data_flags_t

#define NO_NOZZELS  2   // kept to match sanki_6_digit_1.cpp globals

// ---------------------------------------------------------------------------
// app_display_data_t — mirrors old-app's app_common.h definition.
// Field names and sizes must stay identical to sanki_6_digit_1.cpp.
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t       fuel_type;        // display_type_t cast to uint8_t
    unsigned long time_updated;     // epoch seconds (unused in sim path)
    uint64_t      unit_pricex100;   // unit price  × 100
    uint64_t      total_pricex100;  // total price × 100
    uint64_t      volume_lx1000;    // volume (L)  × 1000
    bool          start_stop;       // true = nozzle UP / pumping active
    bool          select_p;
    bool          select_l;
    bool          select_ll;
} app_display_data_t;

// ---------------------------------------------------------------------------
// Helper: fill an app_display_data_t from a display_data_t frame + nozzle flag
// ---------------------------------------------------------------------------
static inline void fuel_types_from_frame(
    app_display_data_t       *out,
    const display_data_t     *frame,
    display_type_t            dtype,
    bool                      nozzle_up)
{
    out->fuel_type      = (uint8_t)dtype;

    switch(dtype)
    {
        case DIS_NONE:
        case DIS_CENSTAR_7_DIGIT:
        case DIS_CENSTAR_7_DIGIT_CS:
        case DIS_HONGYANG_8_DIGIT:
        case DIS_LONGFENG_8_DIGIT:
        case DIS_WAYNE_6_DIGIT:
        case DIS_SANKI_6_DIGIT:        
        {
            out->unit_pricex100 = (uint64_t)frame->unit_price;
            out->total_pricex100= (uint64_t)frame->total_price;
            out->volume_lx1000  = (uint64_t)frame->volume_l;
        }
        break;
        case DIS_CENSTAR_6_DIGIT:
        {
            out->unit_pricex100 = (uint64_t)frame->unit_price*10;
            out->total_pricex100= (uint64_t)frame->total_price*10;
            out->volume_lx1000  = (uint64_t)frame->volume_l;
        }
        break;
        case DIS_WAYNE_6_DIGIT_2:
        {
            // Total and Rate are 1-decimal raw (value*10) -> need *10 to
            // reach the canonical x100 scale. Volume is 3-decimal raw
            // (value*1000), already the canonical x1000 scale.
            out->unit_pricex100 = (uint64_t)frame->unit_price*10;
            out->total_pricex100= (uint64_t)frame->total_price*10;
            out->volume_lx1000  = (uint64_t)frame->volume_l;
        }
        break;
        default:
        break;
    }

    out->start_stop     = nozzle_up;
    out->select_p       = (bool)frame->flags.bits.select_p;
    out->select_l       = (bool)frame->flags.bits.select_l;
    out->select_ll      = (bool)frame->flags.bits.select_ll;
    out->time_updated   = 0;
}
