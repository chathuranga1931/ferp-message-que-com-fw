#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "com_distap.h"


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

// TX_ID_SET_ERR_MASK
typedef enum
{
    ID_CMD_ERR_MASK_ALREADY = 0x00,
    ID_CMD_ERR_MASK_CHANGED = 0xFF
} id_cmd_err_mask_t;
typedef struct
{
    uint8_t err_mask;
} tx_id_set_err_mask_t;
typedef struct
{
    uint8_t state; // send 0xFF if changes, 0x00 already set the display
    uint8_t err_mask;
} rx_id_set_err_mask_t;

// ID RX_ID_INPUTS
typedef enum
{
    ID_CMD_INPUTS_PINS = 0, //reading inputs wihout  changing display enb/dis
    ID_CMD_INPUTS_DIS_ON,
    ID_CMD_INPUTS_DIS_OFF,
    ID_CMD_INPUTS_LED_ON,
    ID_CMD_INPUTS_LED_OFF,
    ID_CMD_INPUTS_CS1_ON,
    ID_CMD_INPUTS_CS1_OFF,
    ID_CMD_INPUTS_CS2_ON,
    ID_CMD_INPUTS_CS2_OFF,
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

esp_err_t distap_get_fw_version(char *ver)
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
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 5*1000); //wait 3 seconds until spiff formwat if want
    if(state && rx_packet.pck_id == RX_ID_DEV_INFO && ((rx_id_dev_version_t*)rx_packet.ab_data)->command == ID_CMD_DEV_VERSION)
    {
        printf("DISTAP Read FW_Ver:%s, display_type:%d\r\n", ((rx_id_dev_version_t*)rx_packet.ab_data)->version, rx_packet.display);
        if(ver)
            strcpy(ver, ((rx_id_dev_version_t*)rx_packet.ab_data)->version);
        return ESP_OK;
    }
    else
    {
        printf("DISTAP FW Version get failed\r\n");
        if(ver) //if not NULL pointer
            ver[0] = '\0';
        return ESP_FAIL;
    }
}

esp_err_t distap_get_fw_name()
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
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
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
esp_err_t distap_get_fw_timedate()
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
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
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

esp_err_t distap_set_display_type(const display_type_t type)
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
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
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

esp_err_t distap_set_err_mask(const data_error_t err)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_SET_ERR_MASK,
        .length = sizeof(tx_id_set_err_mask_t)
    };
    const tx_id_set_err_mask_t err_mask = {
        .err_mask = err.u8int,
    };
    memcpy(tx_packet.ab_data, &err_mask, sizeof(tx_id_set_err_mask_t));
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
    if(state && rx_packet.pck_id == RX_ID_SET_ERR_MASK)
    {
        printf("Set Err Maks:%d, %s\r\n", ((rx_id_set_err_mask_t*)rx_packet.ab_data)->err_mask, ((rx_id_set_err_mask_t*)rx_packet.ab_data)->state?"Success":"Already");
        return ESP_OK;
    }
    else
    {
        printf("Set Err Mask failed\r\n");
        return ESP_FAIL;
    }
}

esp_err_t distap_get_inputs(input_pin_t *pins)
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
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
    if(state && rx_packet.pck_id == RX_ID_INPUTS)
    {
        pins->u32int = ((rx_id_inputs_t*)rx_packet.ab_data)->inputs;
        return ESP_OK;
        // printf("FW_Name:%s\r\n", ((rx_id_dev_prj_name_t*)rx_packet.ab_data)->prj_name);
    }
    else
    {
        // printf("Input Pins get failed\r\n");
        return ESP_FAIL;
    }
}

esp_err_t distap_set_inputenable(bool level)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_INPUTS,
        .length = sizeof(tx_id_inputs_t)
    };
    const tx_id_inputs_t dev = {
        .command = level ? ID_CMD_INPUTS_DIS_ON : ID_CMD_INPUTS_DIS_OFF
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_inputs_t));
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
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

esp_err_t distap_set_led_enable(bool level)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_INPUTS,
        .length = sizeof(tx_id_inputs_t)
    };
    const tx_id_inputs_t dev = {
        .command = level ? ID_CMD_INPUTS_LED_ON : ID_CMD_INPUTS_LED_OFF
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_inputs_t));
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
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

esp_err_t distap_set_cs1_enable(bool level)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_INPUTS,
        .length = sizeof(tx_id_inputs_t)
    };
    const tx_id_inputs_t dev = {
        .command = level ? ID_CMD_INPUTS_CS1_ON : ID_CMD_INPUTS_CS1_OFF
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_inputs_t));
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
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

esp_err_t distap_set_cs2_enable(bool level)
{
    data_packet_t rx_packet = {};
    data_packet_t tx_packet = {
        .pck_id = TX_ID_INPUTS,
        .length = sizeof(tx_id_inputs_t)
    };
    const tx_id_inputs_t dev = {
        .command = level ? ID_CMD_INPUTS_CS2_ON : ID_CMD_INPUTS_CS2_OFF
    };
    memcpy(tx_packet.ab_data, &dev, sizeof(tx_id_inputs_t));
    const bool state = distap_send_cmd(&rx_packet, &tx_packet, 500);
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