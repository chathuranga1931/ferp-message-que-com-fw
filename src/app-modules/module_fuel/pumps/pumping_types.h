
#pragma once

typedef enum {
    Pumping_State_Unknown = 0,
    Pumping_State_Pumping,
    Pumping_State_Stopped,
    Pumping_State_Pumping_Waiting,
    Pumping_State_Stopped_Waiting,
    Pumping_State_Nozzle_Off_Waiting_To_Stable,
}pumping_state_t;

