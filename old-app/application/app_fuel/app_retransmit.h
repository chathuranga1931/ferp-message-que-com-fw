
#pragma once

#include "app_common.h"
#include "storage.h"

typedef enum{

}app_retransmission_event_t;

typedef void (*fp_app_retransmission_on_event_t)(app_retransmission_event_t event, void * arg);
typedef int32_t (*fp_get_storage_interface_t)(storage_interface_t ** storage);

typedef struct {
    fp_app_retransmission_on_event_t fp_app_retransmission_on_event;
    fp_get_storage_interface_t fp_get_storage_interface;
    app_init_t app_init;
}app_retransmit_init_t;


void app_retransmit_init(const app_retransmit_init_t * p_retransmit_init); 
void app_retransmit_run();