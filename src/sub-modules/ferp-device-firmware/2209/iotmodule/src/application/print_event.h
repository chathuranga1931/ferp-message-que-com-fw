
#ifndef __PRINT_EVENT_H
#define __PRINT_EVENT_H

#include <time.h>
#include <Arduino.h>

typedef struct {
    String time_stamp;
    double unit_price;
    double total_price;
    double volume_l;
    String nozzel_id;
    String fuel_type;
} print_event_t;

void print_event_copy(void * dest, void * src);
// String convertNozzelEvent_to_Json(print_event_t * n_event);

#endif //__PRINT_EVENT_H