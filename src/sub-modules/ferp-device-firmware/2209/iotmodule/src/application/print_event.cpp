#include <ArduinoJson.h>

#include "print_event.h"

void print_event_copy(void * dest, void * src){

    print_event_t * n_dest = (print_event_t *)dest;
    print_event_t * n_src = (print_event_t *)src;

    n_dest->time_stamp = n_src->time_stamp;
    n_dest->total_price = n_src->total_price;
    n_dest->unit_price = n_src->unit_price;
    n_dest->volume_l = n_src->volume_l;
    n_dest->fuel_type = n_src->fuel_type;
    n_dest->nozzel_id = n_src->nozzel_id;
}