
#include <ArduinoJson.h>

#include "nozzel_event.h"

void nozzel_event_copy(void * dest, void * src){

    nozzel_event_t * n_dest = (nozzel_event_t *)dest;
    nozzel_event_t * n_src = (nozzel_event_t *)src;

    n_dest->time_stamp = n_src->time_stamp;
    n_dest->total_price = n_src->total_price;
    n_dest->unit_price = n_src->unit_price;
    n_dest->volume_l = n_src->volume_l;
}


