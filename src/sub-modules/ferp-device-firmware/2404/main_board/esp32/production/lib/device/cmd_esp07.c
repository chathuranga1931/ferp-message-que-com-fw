#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "com_esp07.h"


// ID RX_ID_DEV_INFO
typedef enum
{
    ID_CMD_DEV_VERSION = 0,
    ID_CMD_DEV_PROJ_NAME,
    ID_CMD_DEV_TIMEDATE,
    ID_CMD_DEV_SIZE
} id_dev_cmd_t;
typedef struct
{
    uint8_t command;
} tx_id_dev_info_t;
typedef struct
{
    uint8_t command;
    char version[32];
} rx_id_dev_version_t;
typedef struct
{
    uint8_t command;
    char prj_name[32];
} rx_id_dev_prj_name_t;
typedef struct
{
    uint8_t command;
    char time[16];
    char date[16];
} rx_id_dev_timedate_t;

//RX_ID_SET_DISPLAY
typedef enum{
    ID_CMD_DIS_ALREADY = 0x00,
    ID_CMD_DIS_CHANGED = 0xFF
} id_cmd_dis_t;
typedef struct
{
    uint8_t display;
} tx_id_set_display_t;
typedef struct
{
    uint8_t state; //send 0xFF if changes, 0x00 already set the display
    uint8_t display;
}rx_id_set_display_t;

// ID RX_ID_INPUTS
typedef enum
{
    ID_CMD_INPUTS_PINS = 0, //reading inputs wihout  changing display enb/dis
    ID_CMD_INPUTS_ENABLE,
    ID_CMD_INPUTS_DISABLE,
    ID_CMD_INPUTS_SIZE
} id_inputs_cmd_t;
typedef struct
{
    uint8_t command;
} tx_id_inputs_t;
typedef struct
{
    uint32_t inputs;
} rx_id_inputs_t;

esp_err_t esp07_get_fw_version(char *ver)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_DEV_INFO,
        .length = sizeof(tx_id_dev_info_t)
    };
    const tx_id_dev_info_t dev = {
        .command = ID_CMD_DEV_VERSION
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_dev_info_t));
    const bool state = esp07_send_cmd(&rx_packet, &tx_packet, 5*1000); //wait 3 seconds until spiff formwat if want
    if(state && rx_packet.pck_id == RX_ID_DEV_INFO && ((rx_id_dev_version_t*)rx_packet.ab_data)->command == ID_CMD_DEV_VERSION)
    {
        printf("ESP07 Read FW_Ver:%s, display_type:%d\r\n", ((rx_id_dev_version_t*)rx_packet.ab_data)->version, rx_packet.display);
        if(ver)
            strcpy(ver, ((rx_id_dev_version_t*)rx_packet.ab_data)->version);
        return ESP_OK;
    }
    else
    {
        printf("ESP07 FW Version get failed\r\n");
        if(ver) //if not NULL pointer
            ver[0] = '\0';
        return ESP_FAIL;
    }
}

esp_err_t esp07_get_fw_name()
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_DEV_INFO,
        .length = sizeof(tx_id_dev_info_t)
    };
    const tx_id_dev_info_t dev = {
        .command = ID_CMD_DEV_PROJ_NAME
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_dev_info_t));
    const bool state = esp07_send_cmd(&rx_packet, &tx_packet, 500);
    if(state && rx_packet.pck_id == RX_ID_DEV_INFO && ((rx_id_dev_prj_name_t*)rx_packet.ab_data)->command == ID_CMD_DEV_PROJ_NAME)
    {
        printf("FW_Name:%s\r\n", ((rx_id_dev_prj_name_t*)rx_packet.ab_data)->prj_name);
        return ESP_OK;
    }
    else
    {
        printf("FW Name get failed\r\n");
        return ESP_FAIL;
    }
}
esp_err_t esp07_get_fw_timedate()
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_DEV_INFO,
        .length = sizeof(tx_id_dev_info_t)
    };
    const tx_id_dev_info_t dev = {
        .command = ID_CMD_DEV_TIMEDATE
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_dev_info_t));
    const bool state = esp07_send_cmd(&rx_packet, &tx_packet, 500);
    if(state && rx_packet.pck_id == RX_ID_DEV_INFO && ((rx_id_dev_timedate_t*)rx_packet.ab_data)->command == ID_CMD_DEV_TIMEDATE)
    {
        printf("FW Date:%s  Time:%s\r\n", ((rx_id_dev_timedate_t*)rx_packet.ab_data)->date, ((rx_id_dev_timedate_t*)rx_packet.ab_data)->time);
        return ESP_OK;
    }
    else
    {
        printf("FW Time Date get failed\r\n");
        return ESP_FAIL;
    }
}

esp_err_t esp07_set_display_type(display_type_t type)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_SET_DISPLAY,
        .length = sizeof(tx_id_set_display_t)
    };
    const tx_id_set_display_t dis = {
        .display = (uint8_t)type
    };
    memcpy(tx_packet.ab_data, &dis, sizeof(tx_id_set_display_t));
    const bool state = esp07_send_cmd(&rx_packet, &tx_packet, 500);
    if(state && rx_packet.pck_id == RX_ID_SET_DISPLAY)
    {
        printf("Set Display:%d, %s\r\n", ((rx_id_set_display_t*)rx_packet.ab_data)->display, ((rx_id_set_display_t*)rx_packet.ab_data)->state?"Success":"Already");
        return ESP_OK;
    }
    else
    {
        printf("Display Set failed\r\n");
        return ESP_FAIL;
    }
}

esp_err_t esp07_get_inputs(uint32_t *pins)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_INPUTS,
        .length = sizeof(tx_id_inputs_t)
    };
    const tx_id_inputs_t dev = {
        .command = ID_CMD_INPUTS_PINS
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_inputs_t));
    const bool state = esp07_send_cmd(&rx_packet, &tx_packet, 500);
    if(state && rx_packet.pck_id == RX_ID_INPUTS)
    {
        *pins = ((rx_id_inputs_t*)rx_packet.ab_data)->inputs;
        return ESP_OK;
        // printf("FW_Name:%s\r\n", ((rx_id_dev_prj_name_t*)rx_packet.ab_data)->prj_name);
    }
    else
    {
        // printf("Input Pins get failed\r\n");
        return ESP_FAIL;
    }
}

esp_err_t esp07_set_inputenable(bool level)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_INPUTS,
        .length = sizeof(tx_id_inputs_t)
    };
    const tx_id_inputs_t dev = {
        .command = level ? ID_CMD_INPUTS_ENABLE : ID_CMD_INPUTS_DISABLE
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_inputs_t));
    const bool state = esp07_send_cmd(&rx_packet, &tx_packet, 500);
    if(state && rx_packet.pck_id == RX_ID_INPUTS)
    {
        // *pins = ((rx_id_inputs_t*)rx_packet.ab_data)->inputs;
        return ESP_OK;
        // printf("FW_Name:%s\r\n", ((rx_id_dev_prj_name_t*)rx_packet.ab_data)->prj_name);
    }
    else
    {
        // printf("Input Pins get failed\r\n");
        return ESP_FAIL;
    }
}