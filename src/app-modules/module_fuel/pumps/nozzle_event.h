// nozzle_event.h
//
// Nozzle runtime state enum shared by FuelSankiProcessor, ModuleFuel,
// MsgNozzleState, and the sim bridge serialiser.

#pragma once

#include <stdint.h>

typedef enum : uint8_t {
    NOZZLE_IDLE       = 0,   ///< No active transaction
    NOZZLE_PUMPING    = 1,   ///< Fuel is flowing
    NOZZLE_PUMPED     = 2,   ///< Transaction complete (briefly, then → IDLE)
    NOZZLE_TOTALIZER  = 3,   ///< Totalizer reading available for this nozzle
} nozzle_state_t;

/** Data emitted when a complete fuelling transaction is detected. */
typedef struct {
    uint8_t  n_idx;            ///< Nozzle index (0-based)
    uint8_t  event_id;         ///< Reserved / sequence number
    uint64_t time_stamp;       ///< Unix epoch of the transaction
    uint32_t unit_pricex100;   ///< Unit price × 100
    uint64_t total_pricex100;  ///< Total price × 100
    uint32_t volume_lx1000;    ///< Volume in ml (litres × 1000)
} nozzle_event_t;

/** Human-readable string for logging / sim serialisation. */
static inline const char *nozzle_state_str(nozzle_state_t s)
{
    switch (s) {
        case NOZZLE_IDLE:      return "IDLE";
        case NOZZLE_PUMPING:   return "PUMPING";
        case NOZZLE_PUMPED:    return "PUMPED";
        case NOZZLE_TOTALIZER: return "TOTALIZER";
        default:               return "UNKNOWN";
    }
}
