
#ifndef __NOZZEL_EVENT_H
#define __NOZZEL_EVENT_H

#include <time.h>
#include <Arduino.h>

typedef struct {
    long time_stamp;
    double unit_price;
    double total_price;
    double volume_l;
} nozzel_event_t;

void nozzel_event_copy(void * dest, void * src);
String convertNozzelEvent_to_Json(nozzel_event_t * n_event, uint8_t nozzel_id);

#endif //__NOZZEL_EVENT_H