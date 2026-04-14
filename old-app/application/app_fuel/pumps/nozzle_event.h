
#ifndef NOZZLE_EVENT_H
#define NOZZLE_EVENT_H

#include <stdint.h>

typedef struct {
    uint8_t n_idx;
    uint8_t event_id;
    uint64_t time_stamp;
    uint32_t unit_pricex100;
    uint64_t total_pricex100;
    uint32_t volume_lx1000;
} nozzle_event_t;

#endif // NOZZLE_EVENT_H