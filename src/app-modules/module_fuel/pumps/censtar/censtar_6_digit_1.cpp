// censtar_6_digit_1.cpp
//
// Censtar 6-digit dispenser pump driver.
// Logic is identical to sanki_6_digit_1.cpp — the DT board normalises all
// display-tap protocols before the ESP32 sees them.
// GPIO-sourced start_stop is injected by module_fuel into app_display_data_t
// before these functions are called.

#include <stdint.h>

#include "pal_logger.h"
#include "pal_time.h"

#include "fuel_types.h"
#include "censtar_6_digit_1.h"
#include "nozzle_event.h"

#define __TAG__  "APP_CEN6"

#define CEN6_DEBUG_LOG_EN      false
#define CEN6_WARN_LOG_EN       true
#define CEN6_ERROR_LOG_EN      true
#define CEN6_INFO_LOG_EN       true

#define DISPLAY_SAME_COUNT_FOR_STABILIZE_UNIT_PRICE     (10)

static app_display_data_t display_data_validated[NO_NOZZELS];
static app_display_data_t display_data_commited[NO_NOZZELS];
static app_display_data_t display_value_validated_prev[NO_NOZZELS];

static uint64_t display_data_unit_price_new[NO_NOZZELS];
static uint64_t display_data_unit_price[NO_NOZZELS];
static uint32_t display_data_unit_price_new_count[NO_NOZZELS];
static uint32_t display_data_unit_price_count[NO_NOZZELS];

static uint32_t display_tap_event_idx = 0;
static bool is_unit_price_stabilized[NO_NOZZELS] = {0};
static uint32_t pumping_increment_count[NO_NOZZELS];
static pumping_state_t pumping_state_value_based[NO_NOZZELS];
static pumping_state_t pumping_state_nozzle_based[NO_NOZZELS];
static unsigned long pumping_state_ts[NO_NOZZELS] = {0};
static unsigned long pumping_state_nozzle_ts[NO_NOZZELS] = {0};
static unsigned long pumping_state_pumping_ts[NO_NOZZELS] = {0};


static uint64_t corrected_total_price(bool is_updated, uint64_t totalx100){
    
    uint64_t total_incommingx100 = totalx100;
    if(is_updated){

        uint64_t total_inx100 = totalx100;
        uint8_t decimal_value = totalx100%100;
        uint8_t decimal_neg = 100 - decimal_value;
        if(
            (total_inx100 - decimal_value)%1000 == 0
        ){
            total_incommingx100 = (total_inx100 - decimal_value)/100.0;
        }
        else if(
            (total_inx100 + decimal_neg)%1000 == 0
        ){
            total_incommingx100 = (total_inx100 + decimal_neg)/100.0;
        }       
    }
    
    return total_incommingx100;
}


static void stabilizer(
    uint64_t incomming_value, 
    uint64_t * stable_value, 
    uint64_t * new_value, 
    uint32_t * stable_count, 
    uint32_t * new_count, 
    uint32_t same_count_max,
    bool * is_stable)
{

    if((*stable_value) == 0){
        (*stable_value) = incomming_value;
        (*stable_count) = 1;
    }
                    
    if((*stable_value) == incomming_value){
        (*stable_count)++;
        (*new_count) = 0;
        if((*stable_count) > same_count_max){
            (*stable_count) = same_count_max;
        }
    }
    else{        
        if((*new_value) == incomming_value){
            (*new_count)++;
        }else{
            (*new_value) = incomming_value;
            (*new_count) = 1;
        }
    }

    if((*new_count) > (*stable_count)){
        (*stable_value) = (*new_value);
        (*stable_count) = (*new_count);
        (*new_count) = 0;
        (*is_stable) = true;
    } 
}

bool censtar6_get_event(nozzle_event_t * ne, uint8_t idx){

    if(ne != NULL){    
        ne->n_idx = idx;
        ne->time_stamp = display_data_commited[idx].time_updated;
        ne->unit_pricex100 = display_data_commited[idx].unit_pricex100;
        ne->total_pricex100 = display_data_commited[idx].total_pricex100;
        ne->volume_lx1000 = display_data_commited[idx].volume_lx1000;
        return true;
    }

    return false;
}

static bool censtar6_commit_data(uint8_t idx){

    time_t now;
    pal_time_get_epoch_time(&now);

    display_data_commited[idx].fuel_type = display_data_validated[idx].fuel_type;
    display_data_commited[idx].total_pricex100 = display_data_validated[idx].total_pricex100;
    display_data_commited[idx].unit_pricex100 = display_data_validated[idx].unit_pricex100;
    display_data_commited[idx].volume_lx1000 = display_data_validated[idx].volume_lx1000;
    display_data_commited[idx].time_updated = now;
    return true;
}

static void censtar6_update_data(uint8_t idx){

    if(
        (display_data_validated[idx].volume_lx1000 > display_data_commited[idx].volume_lx1000) ||
        (display_data_validated[idx].total_pricex100 > display_data_commited[idx].total_pricex100)){

            time_t now;
            pal_time_get_epoch_time(&now);

            display_data_commited[idx].total_pricex100 = display_data_validated[idx].total_pricex100;
            display_data_commited[idx].unit_pricex100 = display_data_validated[idx].unit_pricex100;
            display_data_commited[idx].volume_lx1000 = display_data_validated[idx].volume_lx1000;
            display_data_commited[idx].time_updated = now;
    }
}

#define ABS(x) (((x) < 0) ? -(x) : (x))

bool censtar6_process_data(app_display_data_t * display_data){

    bool is_valid = false;
    uint8_t error_code_for_debugging = 0;

    do{
        if(display_data->unit_pricex100 < 20000){
            error_code_for_debugging = 11;
            break;
        }

        uint64_t unit_pricex100 = display_data->unit_pricex100;
        uint64_t total_price_expectedx100 = (unit_pricex100*display_data->volume_lx1000)/1000;
        // Use signed arithmetic: integer division can make expected slightly less
        // than actual total, causing uint64_t underflow and a spurious huge gap.
        // The 100-unit tolerance (1.00 LKR) absorbs both x10-format truncation
        // noise (≤ 9 units) and normal integer-division rounding.
        int64_t gapx100 = (int64_t)total_price_expectedx100 - (int64_t)display_data->total_pricex100;

        if((ABS(gapx100) > 200) && display_data->total_pricex100 == 0 && display_data->volume_lx1000 > 0){
            display_data->total_pricex100 = corrected_total_price(true, total_price_expectedx100);
        }    
        else if(ABS(gapx100) > 200){
            if(total_price_expectedx100 >= 10000000){
                display_data->total_pricex100 = display_data->total_pricex100 + (total_price_expectedx100/10000000)*10000000;
                gapx100 = (int64_t)total_price_expectedx100 - (int64_t)display_data->total_pricex100;
                if(ABS(gapx100) > 200){
                    error_code_for_debugging = 8;
                    break;
                }
            }
            else{
                error_code_for_debugging = 5;
                break;
            }
        }

        if((display_data->volume_lx1000 == 0) && (display_data->total_pricex100 > 0)){
            error_code_for_debugging = 6;
            break;
        }

        if((display_data->volume_lx1000 > 0) && (display_data->total_pricex100 == 0)){
            error_code_for_debugging = 7;
            break;
        }

        is_valid = true;

    }while(false);

    return is_valid;
}

void censtar6_data_validate(const app_display_data_t * display_data, uint8_t nozzle_id){
    
    do{ 
        // process_data() has already rejected any half-zero frame (exactly one
        // of volume/total zero), so a frame reaching here is either both-positive
        // (a live reading) or both-zero. BOTH are valid and must update the
        // tracked value: a both-zero reading is a genuine reset (e.g. a fresh
        // nozzle-up), and recording it stops a stale non-zero reading from a
        // previous sale lingering and being re-published as a phantom event.
        // See documents/wayne62-stale-reading-fix.md.
        const bool both_zero     = (display_data->total_pricex100 == 0) && (display_data->volume_lx1000 == 0);
        const bool both_positive = (display_data->total_pricex100 > 0)  && (display_data->volume_lx1000 > 0);
        if(both_positive || both_zero){

            stabilizer(
                display_data->unit_pricex100,
                &(display_data_unit_price[nozzle_id]),
                &(display_data_unit_price_new[nozzle_id]),
                &(display_data_unit_price_count[nozzle_id]),
                &(display_data_unit_price_new_count[nozzle_id]),
                DISPLAY_SAME_COUNT_FOR_STABILIZE_UNIT_PRICE,
                &(is_unit_price_stabilized[nozzle_id])
            );           
               
            display_value_validated_prev[nozzle_id].total_pricex100 = display_data_validated[nozzle_id].total_pricex100;
            display_value_validated_prev[nozzle_id].unit_pricex100 =  display_data_validated[nozzle_id].unit_pricex100;
            display_value_validated_prev[nozzle_id].volume_lx1000 = display_data_validated[nozzle_id].volume_lx1000;
            display_value_validated_prev[nozzle_id].fuel_type = display_data_validated[nozzle_id].fuel_type;

            display_data_validated[nozzle_id].total_pricex100 = display_data->total_pricex100;
            display_data_validated[nozzle_id].unit_pricex100 = display_data->unit_pricex100;
            display_data_validated[nozzle_id].volume_lx1000 = display_data->volume_lx1000;
            display_data_validated[nozzle_id].fuel_type = display_data->fuel_type;
        }

    }while(false);

    return;
}

bool censtar6_process_state_machine(const app_display_data_t * display_data, uint8_t nozzle_id, pumping_state_t * state){

    bool is_pumped_event = false;
    bool is_zeroed = false;

    if( 
        (display_data_validated[nozzle_id].total_pricex100 == 0.0) && 
        (display_data_validated[nozzle_id].volume_lx1000 == 0.0)
    )
    {
        is_zeroed = true;
    }

    int64_t deravative_tot = (display_data_validated[nozzle_id].total_pricex100 - display_value_validated_prev[nozzle_id].total_pricex100);
    int deravative_int = 0;
    
    if (deravative_tot > 0)
    {
        deravative_int = 1;
    }
    else if(deravative_tot < 0)
    {
        deravative_int = -1;
    }

    if(is_zeroed)
    {
        pumping_increment_count[nozzle_id] = 0;
    }
    else if(!(display_data->start_stop))
    {
        pumping_increment_count[nozzle_id] = 0;
    }
    else if(deravative_int == 1)
    {
        pumping_increment_count[nozzle_id]++;
    }

    unsigned long ts = pal_time_get_ms();

    /* virtual pumping state machine */
    switch (pumping_state_value_based[nozzle_id])
    {
        case Pumping_State_Pumping:
            if(deravative_int == 0){
                if((ts - pumping_state_pumping_ts[nozzle_id]) > 500){
                    pumping_state_value_based[nozzle_id] = Pumping_State_Pumping_Waiting;
                    pumping_state_ts[nozzle_id] = ts;
                    LOG_MSG_INFO(CEN6_INFO_LOG_EN, "[Value] State [%d] : Pumping --> Pumping-Waiting", nozzle_id);
                }
            }
            else if(deravative_int == 1){
                pumping_state_pumping_ts[nozzle_id] = ts;
            }
        break;

        case Pumping_State_Pumping_Waiting:
            if(deravative_int == 1){
                pumping_state_value_based[nozzle_id] = Pumping_State_Pumping;
                pumping_state_ts[nozzle_id] = ts;
                pumping_state_pumping_ts[nozzle_id] = ts;
                LOG_MSG_INFO(CEN6_INFO_LOG_EN, "[Value] State [%d] : Pumping-Waiting --> Pumping ", nozzle_id);
            }
        break;

        case Pumping_State_Unknown:
        default:        
            if(deravative_int == 1){
                pumping_state_value_based[nozzle_id] = Pumping_State_Pumping;
                pumping_state_ts[nozzle_id] = ts;
                LOG_MSG_INFO(CEN6_INFO_LOG_EN, "[Value] State [%d] : Unknown --> Pumping ", nozzle_id);
            }
        break;  
    }

    switch(pumping_state_nozzle_based[nozzle_id])
    {
        case Pumping_State_Pumping:
            if(!(display_data->start_stop))
            {                
                if(
                    ((ts - pumping_state_nozzle_ts[nozzle_id]) < 4000) &&
                    (pumping_increment_count[nozzle_id] < 10 )
                )
                {
                    pumping_state_nozzle_ts[nozzle_id] = ts;
                    pumping_state_nozzle_based[nozzle_id] = Pumping_State_Unknown;
                    LOG_MSG_INFO(CEN6_INFO_LOG_EN, "[Nozzle] State [%d] : Pumping --> Unknown ", nozzle_id);
                }
                else
                {
                    pumping_state_nozzle_based[nozzle_id] = Pumping_State_Stopped_Waiting;
                    LOG_MSG_INFO(CEN6_INFO_LOG_EN, "[Nozzle] State [%d] : Pumping --> Stopped-Waiting ", nozzle_id);
                    pumping_state_nozzle_ts[nozzle_id] = ts;
                    censtar6_commit_data(nozzle_id);
                }
            }        
        break;

        case Pumping_State_Stopped_Waiting:

            // Hold for (PUMP_STABILIZE_BASE_MS + g_pump_stabilize_delay_ms) after
            // nozzle DOWN so a late-settling display value is captured before the
            // pumped event is finalized. Non-blocking: frames keep arriving and
            // censtar6_update_data() commits the latest reading each pass.
            if((ts - pumping_state_nozzle_ts[nozzle_id]) > (unsigned long)(PUMP_STABILIZE_BASE_MS + g_pump_stabilize_delay_ms))
            {
                pumping_state_nozzle_based[nozzle_id] = Pumping_State_Stopped;
                pumping_state_nozzle_ts[nozzle_id] = ts;
                LOG_MSG_INFO(CEN6_INFO_LOG_EN, "[Nozzle] State [%d] : Stopped-Waiting --> Stopped ", nozzle_id);
            }
            else
            {
                censtar6_update_data(nozzle_id);
            }   

        break;

        case Pumping_State_Stopped:
            is_pumped_event = true;
            pumping_state_nozzle_based[nozzle_id] = Pumping_State_Unknown;
            pumping_state_nozzle_ts[nozzle_id] = ts;
            LOG_MSG_INFO(CEN6_INFO_LOG_EN, "[Nozzle] State [%d] : Stopped --> Unknown ", nozzle_id);
            break;

        case Pumping_State_Unknown:
        default:        
            if(display_data->start_stop)
            {
                pumping_state_nozzle_based[nozzle_id] = Pumping_State_Pumping;
                pumping_state_nozzle_ts[nozzle_id] = ts;
                LOG_MSG_INFO(CEN6_INFO_LOG_EN, "[Nozzle] State [%d] : Unknown --> Pumping ", nozzle_id);
            }
        break;  
    }

    *state = pumping_state_nozzle_based[nozzle_id];

    return is_pumped_event;
}
