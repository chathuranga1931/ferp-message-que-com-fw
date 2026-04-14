
#include "app_ota.h"
#include <string.h>

#include "pal_fw_update.h"
#include "pal_power.h"

#include "pal_logger.h"
#include "pal_time.h"
#include "pal_fw_update.h"

#include "board.h"
#include "hsys_task.h"
#include "user_config.h"

#include "pal_wifi.h"

#include "app_wifi.h"
#include "app_config.h"
#include "app_print_btn.h"
#include "app_default_btn.h"
#include "app_internet.h"
#include "app_retransmit.h"
#include "app_cloud.h"
#include "app_webserver.h"
#include "app_buzzer.h"
#include "app_fuel.h"
#include "app_disptap.h"
#include "app_time.h"
#include "app_hw.h"
#include "app_mqtt.h"
#include "app_leds.h"

#include "version.h"

#include "hsys_ntp.h"

#include "app.h"
#include "hsys_config.h"
#include "hsys_taskrunner.hpp"

#include "cube_sphere_api.h"

#include "nozzle_event.h"

#define __TAG__  "APP     "

#define APP_DEB_LOG_EN      LOG_DIS
#define APP_WRN_LOG_EN      LOG_DIS
#define APP_ERR_LOG_EN      LOG_DIS
#define APP_INF_LOG_EN      LOG_EN

extern app_config_t _app_config;

static uint8_t device_ip_address[SIZE_OF_IP_ADDRESS] = "---";
static int8_t device_rssi_level = -100;

typedef enum {
    ota_driver_idx_esp32main = 0,
    ota_driver_idx_esp07dt = 1,

    ota_driver_idx_count
} ota_drver_list_idx_t;   

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                           APP COMPONENTS                                              */
/*====================================================================================================== */
enum AppId_t
{
    APP_ID_CONFIG,
    APP_ID_WIFI,
    APP_ID_CLOUD,
    APP_ID_WEBSERVER,
    APP_ID_BUZZER,
    APP_ID_FUEL,
    APP_ID_ESP07,
    APP_ID_HW,
    APP_ID_BTN_PRINT,
    APP_ID_BTN_DEFAULT,
    APP_ID_MQTT,
    APP_ID_INTERNET,
    APP_ID_RETXMIT,
    APP_ID_OTA,
    APP_ID_TIMING,
    APP_ID_LED,
    APP_ID_SD,
    APP_ID_SPIFFS,

    APP_ID_MAX
};
event_table_t _event_tables[APP_ID_MAX] = {0};
int _no_event_tables = APP_ID_MAX;


// Commented out until modules are implemented
std::vector<hsys_taskrunner::fp_app_run_t> basic_processes = { app_default_btn_run, app_print_btn_run, app_config_run, app_timeman_run };
std::vector<hsys_taskrunner::fp_app_run_t> newtwork_processes = { app_internet_run, app_cloud_run, app_wifi_run, app_webserver_run, app_ota_run, app_mqtt_run };  
std::vector<hsys_taskrunner::fp_app_run_t> fuel_processes = { app_retransmit_run, app_disptap_run, app_fuel_run };  
std::vector<hsys_taskrunner::fp_app_run_t> hw_processes = { app_hw_run, app_buzzer_run };  

hsys_taskrunner base_tasks         ("Base"       ,   8*1024,   5,    10, basic_processes       , false  );
hsys_taskrunner network_tasks      ("Network"    ,  10*1024,   5,    10, newtwork_processes    , false  );
hsys_taskrunner hw_tasks           ("HW"         ,  10*1024,  10,    10, hw_processes          , false  );
hsys_taskrunner fuel_tasks         ("fuel"       ,  10*1024,  10,    10, fuel_processes        , false  );


/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                              Root CA                                                  */
/*====================================================================================================== */
const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n"
"MIIFYjCCBEqgAwIBAgIQd70NbNs2+RrqIQ/E8FjTDTANBgkqhkiG9w0BAQsFADBX\n"
"MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n"
"CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIwMDYx\n"
"OTAwMDA0MloXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n"
"GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFIx\n"
"MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAthECix7joXebO9y/lD63\n"
"ladAPKH9gvl9MgaCcfb2jH/76Nu8ai6Xl6OMS/kr9rH5zoQdsfnFl97vufKj6bwS\n"
"iV6nqlKr+CMny6SxnGPb15l+8Ape62im9MZaRw1NEDPjTrETo8gYbEvs/AmQ351k\n"
"KSUjB6G00j0uYODP0gmHu81I8E3CwnqIiru6z1kZ1q+PsAewnjHxgsHA3y6mbWwZ\n"
"DrXYfiYaRQM9sHmklCitD38m5agI/pboPGiUU+6DOogrFZYJsuB6jC511pzrp1Zk\n"
"j5ZPaK49l8KEj8C8QMALXL32h7M1bKwYUH+E4EzNktMg6TO8UpmvMrUpsyUqtEj5\n"
"cuHKZPfmghCN6J3Cioj6OGaK/GP5Afl4/Xtcd/p2h/rs37EOeZVXtL0m79YB0esW\n"
"CruOC7XFxYpVq9Os6pFLKcwZpDIlTirxZUTQAs6qzkm06p98g7BAe+dDq6dso499\n"
"iYH6TKX/1Y7DzkvgtdizjkXPdsDtQCv9Uw+wp9U7DbGKogPeMa3Md+pvez7W35Ei\n"
"Eua++tgy/BBjFFFy3l3WFpO9KWgz7zpm7AeKJt8T11dleCfeXkkUAKIAf5qoIbap\n"
"sZWwpbkNFhHax2xIPEDgfg1azVY80ZcFuctL7TlLnMQ/0lUTbiSw1nH69MG6zO0b\n"
"9f6BQdgAmD06yK56mDcYBZUCAwEAAaOCATgwggE0MA4GA1UdDwEB/wQEAwIBhjAP\n"
"BgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBTkrysmcRorSCeFL1JmLO/wiRNxPjAf\n"
"BgNVHSMEGDAWgBRge2YaRQ2XyolQL30EzTSo//z9SzBgBggrBgEFBQcBAQRUMFIw\n"
"JQYIKwYBBQUHMAGGGWh0dHA6Ly9vY3NwLnBraS5nb29nL2dzcjEwKQYIKwYBBQUH\n"
"MAKGHWh0dHA6Ly9wa2kuZ29vZy9nc3IxL2dzcjEuY3J0MDIGA1UdHwQrMCkwJ6Al\n"
"oCOGIWh0dHA6Ly9jcmwucGtpLmdvb2cvZ3NyMS9nc3IxLmNybDA7BgNVHSAENDAy\n"
"MAgGBmeBDAECATAIBgZngQwBAgIwDQYLKwYBBAHWeQIFAwIwDQYLKwYBBAHWeQIF\n"
"AwMwDQYJKoZIhvcNAQELBQADggEBADSkHrEoo9C0dhemMXoh6dFSPsjbdBZBiLg9\n"
"NR3t5P+T4Vxfq7vqfM/b5A3Ri1fyJm9bvhdGaJQ3b2t6yMAYN/olUazsaL+yyEn9\n"
"WprKASOshIArAoyZl+tJaox118fessmXn1hIVw41oeQa1v1vg4Fv74zPl6/AhSrw\n"
"9U5pCZEt4Wi4wStz6dTZ/CLANx8LZh1J7QJVj2fhMtfTJr9w4z30Z209fOU0iOMy\n"
"+qduBmpvvYuR7hZL6Dupszfnw0Skfths18dG9ZKb59UhvmaSGZRVbNQpsg3BZlvi\n"
"d0lIKO2d1xozclOzgjXPYovJJIultzkMu34qQb9Sz/yilrbCgj8=\n"
"-----END CERTIFICATE-----\n";

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                           Configurations                                              */
/*====================================================================================================== */
#define ADD_NOZZLE_PRINT_CONFIG(index) \
    { "nozzel_id_for_print_" #index,        HSYS_TYPE_STRING,         _app_config.app.nozzle[index].nozzel_id_for_print,        SIZE_OF_NOZZELID           }, \
    { "fuel_type_str_for_print_" #index,    HSYS_TYPE_STRING,         _app_config.app.nozzle[index].fuel_type_str_for_print,    SIZE_OF_FUEL_TYPE_STR      }

#define ADD_NOZZLE_CLOUD_CONFIG(index) \
	{ "uuid_" #index,                       HSYS_TYPE_STRING,         _app_config.app.fix_nzzle[index].uuid,    	            SIZE_OF_UUID               }, \
	{ "nozzel_id_" #index,                  HSYS_TYPE_STRING,         _app_config.app.fix_nzzle[index].nozzle_id,               SIZE_OF_NOZZELID           }, \
	{ "fuel_type_" #index,                  HSYS_TYPE_STRING,         _app_config.app.fix_nzzle[index].fuel_type,               SIZE_OF_FUEL_TYPE          }, \
	{ "fuel_type_str_" #index,              HSYS_TYPE_STRING,         _app_config.app.fix_nzzle[index].fuel_type_str,           SIZE_OF_FUEL_TYPE_STR      }


#define NO_CONFIGS					(4 + (6 * NO_NOZZELS) + 15)
static config_t _tbl_configs[NO_CONFIGS] = {
	{ "ssid",                               HSYS_TYPE_STRING,         _app_config.wifi.config.sta.ssid,                                    SIZE_OF_WIFI_SSID          },
	{ "password",                           HSYS_TYPE_STRING,         _app_config.wifi.config.sta.password,                                SIZE_OF_WIFI_PASSWORD      },
	ADD_NOZZLE_PRINT_CONFIG(0),
	ADD_NOZZLE_PRINT_CONFIG(1),
    { "cloud_url",                          HSYS_TYPE_STRING,         _app_config.app.url, 		                                SIZE_OF_NTWK_BASE_URL      },
    { "cloud_secret",                       HSYS_TYPE_STRING,         _app_config.app.secret,                                   SIZE_OF_SECRET             },
    ADD_NOZZLE_CLOUD_CONFIG(0),
    ADD_NOZZLE_CLOUD_CONFIG(1),	
    { "display_type",                       HSYS_TYPE_UINT32,         &_app_config.app.display_type,                            sizeof(uint32_t)            },
	{ "printer_url",                        HSYS_TYPE_STRING,         _app_config.printer.url, 		                            SIZE_OF_NTWK_BASE_URL       },
	{ "p_cpy_cnt",                          HSYS_TYPE_UINT32,         &_app_config.printer.print_copy_count,                    sizeof(uint32_t)            },
	{ "en_udp_ser",                         HSYS_TYPE_BOOL,           &_app_config.logging.udp_log_enabled,                     sizeof(bool)                },		
	{ "en_nid_prnt",                        HSYS_TYPE_BOOL,           &_app_config.app.enable_nid_print,                        sizeof(bool)                },
	{ "en_nid_cloud",                       HSYS_TYPE_BOOL,           &_app_config.app.enable_nid_cloud,                        sizeof(bool)                },
	{ "dt_log_rate",                        HSYS_TYPE_UINT32,         &_app_config.app.dt_log_rate,                             sizeof(uint32_t)            },
	{ "udp_srvr_ip",                        HSYS_TYPE_STRING,         _app_config.logging.udp_server_ip, 	                    SIZE_OF_IPADDRESS           },
	{ "udp_srvr_port",                      HSYS_TYPE_UINT32,         &_app_config.logging.udp_server_port,                     sizeof(uint32_t)            },
	{ "prnt_delay",                         HSYS_TYPE_UINT32,         &_app_config.printer.print_delay_ms,                      sizeof(uint32_t)            },
	{ "cptr_delay",                         HSYS_TYPE_UINT32,         &_app_config.app.stabilize_delay_ms,                      sizeof(uint32_t)            },
	{ "hb_interval",                        HSYS_TYPE_UINT32,         &_app_config.app.hb_interval_s,                           sizeof(uint32_t)            },
    { "mqtt_host",                          HSYS_TYPE_STRING,         _app_config.mqtt.broker_uri, 	                                SIZE_OF_MQTT_HOST           },
    { "mqtt_port",                          HSYS_TYPE_UINT32,         &_app_config.mqtt.port, 	                                sizeof(uint32_t)            },
    { "device_group",                       HSYS_TYPE_STRING,         _app_config.group, 	                                    SIZE_OF_DEVICE_GROUP        },
};

void _app_config_load_default()
{
    strcpy(_app_config.wifi.config.sta.ssid, DEFAULT_WIFI_SSID);
    strcpy(_app_config.wifi.config.sta.password, DEFAULT_WIFI_PW);    
    _app_config.wifi.mode = DEFAULT_WIFI_MODE;

    for(int i=0; i<NO_NOZZELS; i++){
        strcpy(_app_config.app.nozzle[i].nozzel_id_for_print,       DEFAULT_NOZZEL_ID);
        strcpy(_app_config.app.nozzle[i].fuel_type_str_for_print,   DEFAULT_FUEL_TYPE_STR);
    }
    _app_config.app.display_type = DEFAULT_DISPLAY_TYPE;
    strcpy(_app_config.app.printer_url, DEFAULT_PRINTER_URL);
    
    _app_config.wifi.mode = DEFAULT_WIFI_MODE;
    _app_config.wifi.rssi_no_levels = DEFAULT_RSSI_NO_LEVELS;

    strcpy(_app_config.mqtt.broker_uri, DEFAULT_MQTT_HOST);
    _app_config.mqtt.port = DEFAULT_MQTT_PORT;
    strcpy(_app_config.group, DEFAULT_DEVICE_GROUP);
}

void app_config_on_event(app_config_event_t event, void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "on config event %d", (int)event);
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_config_event){
            _event_tables[i].on_config_event(event, arg);
        }
    }
}

const app_config_init_t _app_config_init = 
{
    .fp_app_config_on_event = app_config_on_event,
    .config_table = _tbl_configs,
    .config_table_size = NO_CONFIGS,
    .app_init = {
        .event_table = &_event_tables[APP_ID_CONFIG],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &base_tasks
    }
};


/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                              SPIFFS                                                   */
/*====================================================================================================== */
void app_spiffs_on_event(app_spiffs_event_t event, void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "on SPIFFS event %d", (int)event);
    for(int i=0; i<_no_event_tables; i++)
    {
        if(_event_tables[i].on_spiffs_event)
        {
            _event_tables[i].on_spiffs_event(event, arg);
        }
    }

    if(event == APP_SPIFFS_EVNT_SPIFFS_READY)
    {
        LOG_MSG_DEBUG(APP_DEB_LOG_EN, "SPIFFS is ready, can proceed with OTA or other operations that depend on SPIFFS");

        LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Configure OTA for ESP32 main firmware");
        app_ota_on_driver_ready(ota_driver_idx_esp32main);
    }
}


/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                                SD                                                     */
/*====================================================================================================== */
static uint64_t _sd_card_size = 0;
static bool _is_sd_card_ready = false;
void app_sd_on_event(app_sd_event_t event, void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "on SD event %d", (int)event);
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_sd_event){
            _event_tables[i].on_sd_event(event, arg);
        }
    }

    switch(event){
        case APP_SD_EVNT_SD_READY:
            LOG_MSG_DEBUG(APP_DEB_LOG_EN, "SD card is ready");
            if(arg != NULL)
            {
                uint64_t * card_size = (uint64_t *)arg;
                _sd_card_size = *card_size;
                _is_sd_card_ready = true;
                LOG_MSG_DEBUG(APP_DEB_LOG_EN, "SD Card Size: %lldMB", _sd_card_size);
            } 
            else 
            {
                LOG_MSG_DEBUG(APP_DEB_LOG_EN, "SD Card Size: Unknown");
            }        
        break;
        
        default:
            LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Unhandled SD event: %d", (int)event);
        break;
    }
}

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                                WiFi                                                   */
/*====================================================================================================== */
int32_t app_config_get_wifi_init(pal_wifi_init_config_t * wifi_init, uint32_t timeout_ms)
{
    int32_t ret = ERROR_APP_INVALID_ARGUMENTS;

    if(wifi_init == nullptr){
        return ret;
    }

    unsigned long ts = board_millis();
    ret = app_config_get_config("ssid", &(wifi_init->config.sta.ssid), timeout_ms);
    if(ret != ERROR_OK){
        goto exit;
    }    

    timeout_ms = timeout_ms - (board_millis() - ts);
    ret = app_config_get_config("password", &(wifi_init->config.sta.password), timeout_ms);    
    if(ret != ERROR_OK){
        goto exit;
    }

    wifi_init->mode = DEFAULT_WIFI_MODE;
    wifi_init->rssi_no_levels = DEFAULT_RSSI_NO_LEVELS;
    
    ret = ERROR_OK;
    exit:
    return ret;
}

void app_wifi_on_event(app_wifi_event_t event, void * arg)
{    
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_wifi_event){
            _event_tables[i].on_wifi_event(event, arg);
        }
    }

    // switch(event){
        // case APP_WIFI_EVENT HSYS_WIFI_EVENT_STA_RSSI_CHANGED:
        // {
        //     int8_t * rssi = (int8_t *)arg;
        //     if(rssi != NULL){
        //         device_rssi_level = *rssi;
        //         LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Device RSSI Level: %d", device_rssi_level);
        //     }
        // }
        // break;

        // case APP_WIFI_EVENT_STA_GOT_IP:
        // {
        //     if(arg != NULL){
        //         pal_wifi_t * wifi = (pal_wifi_t *)arg;
        //         if(wifi != NULL){
        //             memcpy(device_ip_address, wifi->ip_addr, SIZE_OF_IP_ADDRESS);
        //             LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Device IP Address: %s", wifi->ip_addr);
        //         }
        //     }
        // }
        // break;

        // default:
        // {

        // }
        // break;
    // }
}

const app_wifi_init_t _app_wifi_init = 
{
    .fp_app_wifi_on_event = app_wifi_on_event,
    .fp_app_wifi_get_init_config = app_config_get_wifi_init,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_WIFI],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &network_tasks
    }
};

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                           PRINT BUTTONS                                               */
/*====================================================================================================== */
void app_print_btn_on_event(app_print_btn_event_t event, void * arg)
{
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_print_btn_event){
            _event_tables[i].on_print_btn_event(event, arg);
        }
    }
}

const app_print_btn_init_t _app_print_btn_init = 
{
    .fp_app_print_btn_on_event = app_print_btn_on_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_BTN_PRINT],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &base_tasks
    }
};

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                          DEFAULT BUTTON                                               */
/*====================================================================================================== */
void app_default_btn_on_event(app_default_btn_event_t event, void * arg)
{
    switch(event){
        case APP_DEFAULT_BTN_SHORT_PRESSED:
            break;
        case APP_DEFAULT_BTN_LONG_PRESSED:
            break;
        default:
            break;
    }
}

const app_default_btn_init_t _app_default_btn_init = 
{
    .fp_app_default_btn_on_event = app_default_btn_on_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_BTN_DEFAULT],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &base_tasks
    }
};


/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                             INTERNET                                                  */
/*====================================================================================================== */
void _on_internet_event(app_internet_event_t event, void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Internet event: %d", (int)event);
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_internet_event){
            _event_tables[i].on_internet_event(event, arg);
        }
    }
}

const app_internet_init_t _app_internet_init = 
{
    .fp_app_internet_on_event = _on_internet_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_INTERNET],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &network_tasks
    }
};



/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                           RETRANSMISSION                                              */
/*====================================================================================================== */
void app_retransmission_on_event(app_retransmission_event_t event, void * arg)
{

}

#include "storage.h"
storage_interface_t _storage_interface = 
{
    .delete_file = (storage_delete_file_t)app_sd_delete_file,
    .create_file = (storage_create_file_t)app_sd_create_file,
    .append_line = (storage_append_line_t)app_sd_append_line,
    .write_file = (storage_write_file_t)app_sd_write_file,
    .read_file = (storage_read_file_t)app_sd_read_file,
    .remove_dir = (storage_remove_dir_t)app_sd_remove_dir,
    .read_line = (storage_read_line_t)app_sd_read_line,
    .get_next_file = nullptr
};

int32_t app_get_storage_interface(storage_interface_t ** storage)
{
    if(storage == nullptr){
        return ERROR_APP_INVALID_ARGUMENTS;
    }

    *storage = &_storage_interface;
    
    return ERROR_OK;
}

const app_retransmit_init_t _app_retransmit_init = 
{
    .fp_app_retransmission_on_event = app_retransmission_on_event,
    .fp_get_storage_interface = app_get_storage_interface,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_RETXMIT],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &fuel_tasks
    }
};


/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                                CLOUD                                                  */
/*====================================================================================================== */
static uint32_t _app_fuel_event_count = 0;

static char _esp07_fw_version[SIZE_OF_DISPTAP_FW_VERSION] = {0};

int32_t app_cloud_on_cloud_register_rqst(void * arg)
{
    uint8_t mac_address[8];
    board_get_mac_address(mac_address, 8);

    char mac_address_str[SIZE_OF_MAC];
    sprintf(mac_address_str, "%02X%02X%02X%02X%02X%02X", 
        mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "%s", mac_address_str);

    // run every one minute if failed to get the configurations
    int32_t ret = ERROR_OK;
    ret = cube_sphere_register(mac_address_str, root_ca);
    if(ret == ERROR_OK){
        nozzel_config_t nozzle_config[NO_NOZZELS] = {0};
        cube_sphere_get_nozzle_config(nozzle_config);
    }
    
    return ret;
}

int32_t app_cloud_on_cloud_hb_rqst(void * arg)
{    
    heart_beat_info_t hb = {0};

    hb.rssi = device_rssi_level;
    hb.uptime_sec = board_millis() / 1000; // convert to seconds
    hb.nozzle_event_count_success = _app_fuel_event_count;
    hb.nozzle_event_count_failure = _app_fuel_event_count;

    // TODO fill info
    int32_t ret = cube_sphere_send_hb(hb);
    return ERROR_OK;
}

int32_t app_cloud_on_cloud_event_reconnect_rqst(void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Sending reconnect event to cloud");

    int32_t ret = ERROR_OK;

    reconnect_info_t reconnect = {0};

    reconnect.uptime_sec = board_millis() / 1000; // convert to seconds
    reconnect.rssi = device_rssi_level;
    memcpy(reconnect.ip_address, device_ip_address, SIZE_OF_IP_ADDRESS);
    memcpy(reconnect.ssid, _app_config.wifi.config.sta.ssid, SIZE_OF_WIFI_SSID);
    memcpy(reconnect.password, _app_config.wifi.config.sta.password, SIZE_OF_WIFI_PASSWORD);

    ret = cube_sphere_send_reconnect(reconnect);
    return ret;
}

int32_t app_cloud_on_cloud_event_status_updated(void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Sending status updated event to cloud");

    int32_t ret = ERROR_OK;
    // TODO fill info
    startup_info_t startup = {0};

    startup.uptime_sec = board_millis() / 1000; // convert to seconds
    startup.rssi = device_rssi_level;
    memcpy(startup.ip_address, device_ip_address, SIZE_OF_IP_ADDRESS);
    memcpy(startup.ssid, _app_config.wifi.config.sta.ssid, SIZE_OF_WIFI_SSID);
    memcpy(startup.password, _app_config.wifi.config.sta.password, SIZE_OF_WIFI_PASSWORD);

    uint8_t mac_address[8];
    board_get_mac_address(mac_address, 8);
    sprintf(startup.mac_address_str, "%02X:%02X:%02X:%02X:%02X:%02X", 
        mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
    startup.nozzle_event_count_success = _app_fuel_event_count;
    startup.nozzle_event_count_failure = _app_fuel_event_count;
    sprintf(startup.sd_card_size_str, "%lldMB", _sd_card_size);
    sprintf(startup.sd_card_status, "%s", _is_sd_card_ready ? "Ready" : "Not Ready");    
    sprintf(startup.fw_version, "%s", FW_VERSION);
    sprintf(startup.hw_version, "%s", HW_VERSION);
    sprintf(startup.board_version, "%s", BOARD_VERSION);
    sprintf(startup.device_type, "%s", DEVICE_TYPE);
    sprintf(startup.esp07_fw_version, "%s", _esp07_fw_version);

    ret = cube_sphere_send_status_updated(startup);
    return ret;
}

int32_t app_cloud_on_cloud_event_startup_rqst(void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Sending startup event to cloud");

    int32_t ret = ERROR_OK;
    // TODO fill info
    startup_info_t startup = {0};

    startup.uptime_sec = board_millis() / 1000; // convert to seconds
    startup.rssi = device_rssi_level;
    memcpy(startup.ip_address, device_ip_address, SIZE_OF_IP_ADDRESS);
    memcpy(startup.ssid, _app_config.wifi.config.sta.ssid, SIZE_OF_WIFI_SSID);
    memcpy(startup.password, _app_config.wifi.config.sta.password, SIZE_OF_WIFI_PASSWORD);

    uint8_t mac_address[8];
    board_get_mac_address(mac_address, 8);
    sprintf(startup.mac_address_str, "%02X:%02X:%02X:%02X:%02X:%02X", 
        mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
    startup.nozzle_event_count_success = _app_fuel_event_count;
    startup.nozzle_event_count_failure = _app_fuel_event_count;
    sprintf(startup.sd_card_size_str, "%lldMB", _sd_card_size);
    sprintf(startup.sd_card_status, "%s", _is_sd_card_ready ? "Ready" : "Not Ready");    
    sprintf(startup.fw_version, "%s", FW_VERSION);
    sprintf(startup.hw_version, "%s", HW_VERSION);
    sprintf(startup.board_version, "%s", BOARD_VERSION);
    sprintf(startup.device_type, "%s", DEVICE_TYPE);
    sprintf(startup.esp07_fw_version, "%s", _esp07_fw_version);

    ret = cube_sphere_send_startup(startup);
    return ret;
}

int32_t app_cloud_on_cloud_event_pumped_rqst(void * arg)
{
    int32_t ret = ERROR_OK;

    if(arg == nullptr){
        return ERROR_APP_INVALID_ARGUMENTS;
    }

    nozzle_event_t * ne = (nozzle_event_t *)arg;

    pumped_event_info_t event = {0};
    event.n_idx = ne->n_idx;
    event.time_stamp = ne->time_stamp;
    event.unit_pricex100 = ne->unit_pricex100;
    event.total_pricex100 = ne->total_pricex100;
    event.volume_lx1000 = ne->volume_lx1000;

    LOG_MSG_DEBUG(APP_INF_LOG_EN, "Nozzle %d pumped event: volume=%.3fL, unit_price=%.2f, total_price=%.2f, time stamp=%lld", 
        event.n_idx, event.volume_lx1000 / 1000.0, event.unit_pricex100 / 100.0, event.total_pricex100 / 100.0, event.time_stamp);

    ret = cube_sphere_send_pumped(event);
    return ret;
}

int32_t app_cloud_on_cloud_event_printed_rqst(void * arg)
{

    int32_t ret = ERROR_OK;

    if(arg == nullptr){
        return ERROR_APP_INVALID_ARGUMENTS;
    }

    nozzle_event_t * ne = (nozzle_event_t *)arg;

    // TODO fill info
    pumped_event_info_t event = {0};
    event.n_idx = ne->n_idx;
    event.time_stamp = ne->time_stamp;
    event.unit_pricex100 = ne->unit_pricex100;
    event.total_pricex100 = ne->total_pricex100;
    event.volume_lx1000 = ne->volume_lx1000;

    ret = cube_sphere_send_printed(event);
    return ret;
}

const cloud_driver_t cube_spear_cloud = 
{
    .fp_on_cloud_register_rqst = app_cloud_on_cloud_register_rqst,

    .fp_on_cloud_event_startup_rqst = app_cloud_on_cloud_event_startup_rqst,
    .fp_on_cloud_event_status_updated = app_cloud_on_cloud_event_status_updated,
    .fp_on_cloud_event_hb_rqst = app_cloud_on_cloud_hb_rqst,
    .fp_on_cloud_event_reconnect_rqst = app_cloud_on_cloud_event_reconnect_rqst,
    
    .fp_on_cloud_event_pumped_rqst = app_cloud_on_cloud_event_pumped_rqst,
    .fp_on_cloud_event_printed_rqst = app_cloud_on_cloud_event_printed_rqst,
    .fp_get_device_uuid = cube_sphere_get_device_uuid,
};

void app_cloud_on_event(app_cloud_event_t event, void * arg)
{
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_cloud_event){
            _event_tables[i].on_cloud_event(event, arg);
        }
    }

    switch(event){
        case APP_CLOUD_EVNT_NETWORK_CONFIG_READY:
            LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Cloud Config ready, can start OTA process");
            app_ota_device_network_connected();
            break;
        default:
            break;
    }
}

const app_cloud_init_t _app_cloud_init = 
{
    .fp_app_cloud_on_event = app_cloud_on_event,
    .drv = &cube_spear_cloud,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_CLOUD],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &network_tasks
    }
};


/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                             WEB SERVER                                                */
/*====================================================================================================== */
void app_webserver_on_event(app_webserver_event_t event, void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "on webserver event %d", (int)event);
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_webserver_event){
            _event_tables[i].on_webserver_event(event, arg);
        }
    }
}

static pal_fw_update_handle_t _fw_update_handle = NULL;
static bool _fw_update_in_progress = false;
static uint32_t _fw_update_start_ts = 0;
#define OTA_TIMEOUT         (300000)    // 5 minutes

int32_t aws_on_esp32bin_file_received(const char * filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if(index == 0)
    {
        LOG_MSG_DEBUG(APP_DEB_LOG_EN, "FW update started, file: %s", filename);

        if(_fw_update_in_progress && ((pal_time_get_ms() - _fw_update_start_ts) < OTA_TIMEOUT))
        {
            LOG_MSG_ERROR(APP_DEB_LOG_EN, "FW update already in progress");
            return ERROR_FAILED;
        }

        int32_t ret = pal_fw_update_begin(&_fw_update_handle);
        if(ret != PAL_OK)
        {
            LOG_MSG_ERROR(APP_DEB_LOG_EN, "pal_fw_update_begin failed: %d", ret);
            return ERROR_FAILED;
        }

        _fw_update_start_ts = pal_time_get_ms();
        _fw_update_in_progress = true;
    }

    if(_fw_update_in_progress)
    {
        int32_t ret = pal_fw_update_write(_fw_update_handle, data, len);
        if(ret != PAL_OK)
        {
            LOG_MSG_ERROR(APP_DEB_LOG_EN, "pal_fw_update_write failed: %d", ret);
            pal_fw_update_abort(_fw_update_handle);
            _fw_update_handle = NULL;
            _fw_update_in_progress = false;
            return ERROR_FAILED;
        }

        if(final)
        {
            ret = pal_fw_update_end(_fw_update_handle);
            _fw_update_handle = NULL;
            _fw_update_in_progress = false;

            if(ret == PAL_OK)
            {
                // LOG_MSG_DEBUG(APP_DEB_LOG_EN, "FW update complete — rebooting");
                // pal_power_reset();
            }
            else
            {
                LOG_MSG_ERROR(APP_DEB_LOG_EN, "pal_fw_update_end failed: %d", ret);
                return ERROR_FAILED;
            }
        }
    }

    return ERROR_OK;
}

const char* aws_on_get_ota_status_string()
{
    static pal_fw_update_status_t status;
    pal_fw_update_get_status(&status);
    return status.status_str;
}

#include "crc32.h"

typedef struct {
    uint32_t crc32;
    uint32_t file_size;
    uint8_t file_path[128];
    enum esp07_ota_file_download_state_t{
        ESP07_OTA_FILE_DOWNLOAD_IDLE = 0,
        ESP07_OTA_FILE_DOWNLOAD_INPROGRESS,
        ESP07_OTA_FILE_DOWNLOAD_FINISHED
    }  state;
} esp07_ota_file_download_meta_t;

static esp07_ota_file_download_meta_t esp07_ota_file_download_meta = {0};

int32_t aws_on_esp07bin_file_received(const char * filename, size_t index, uint8_t *data, size_t len, bool final) 
{    
    int32_t ret;
	if (!index) 
    {
		LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Upload Start: %s", filename);        
        sprintf((char *)esp07_ota_file_download_meta.file_path, "/esp07/%s", filename);
        
        ret = app_spiffs_delete_file((const char *)esp07_ota_file_download_meta.file_path, 1000);
        if(ret != ERROR_OK)
        {
            LOG_MSG_ERROR(APP_DEB_LOG_EN, "Failed to delete existing file: %s", esp07_ota_file_download_meta.file_path);
            return ERROR_FAILED;
        }

        ret = app_spiffs_create_file((const char *)esp07_ota_file_download_meta.file_path, 1000);
        if(ret != ERROR_OK)
        {
            LOG_MSG_ERROR(APP_DEB_LOG_EN, "Failed to create file: %s", esp07_ota_file_download_meta.file_path);
            return ERROR_FAILED;
        }

		esp07_ota_file_download_meta.crc32 = 0;
        esp07_ota_file_download_meta.file_size = 0;
        esp07_ota_file_download_meta.state = esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_IDLE;
	}

	if (len) 
    {	
        ret = app_spiffs_append_file((const char *)esp07_ota_file_download_meta.file_path, (const uint8_t *)data, len, 2000);
        if(ret != ERROR_OK)
        {
            LOG_MSG_ERROR(APP_DEB_LOG_EN, "Failed to append data to file: %s", esp07_ota_file_download_meta.file_path);
            return ERROR_FAILED;
        }        
        esp07_ota_file_download_meta.crc32 = crc32_update(esp07_ota_file_download_meta.crc32, data, len);
        esp07_ota_file_download_meta.file_size += len;
        esp07_ota_file_download_meta.state = esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_INPROGRESS;
	}

	if (final) 
    {
        LOG_MSG_DEBUG(APP_DEB_LOG_EN, "File %s received successfully, size: %d bytes", filename, esp07_ota_file_download_meta.file_size);
        esp07_ota_file_download_meta.state = esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_FINISHED;
	}
    
    return ERROR_OK;
}

const char* aws_on_get_esp07ota_status_string()
{
    static char status[64] = {0};
    switch(esp07_ota_file_download_meta.state)
    {
        case esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_IDLE:
            snprintf(status,  64, "IDLE ");
            break;
        case esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_INPROGRESS:
            snprintf(status,  64, "INPRO %ld", esp07_ota_file_download_meta.file_size);
            break;
        case esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_FINISHED:
            snprintf(status,  64, "CMPLT %08lX", esp07_ota_file_download_meta.crc32);
            break;
    }

    return status;
}

web_server_cb_t aws_cb_table = 
{
    .fp_on_esp32bin_file_received = aws_on_esp32bin_file_received,
    .fp_on_esp32bin_file_get_status_string = aws_on_get_ota_status_string,
    .fp_on_esp07bin_file_received = aws_on_esp07bin_file_received,
    .fp_on_esp07bin_file_get_status_string = aws_on_get_esp07ota_status_string,
};

const app_webserver_init_t _app_webserver_init = 
{
    .fp_app_webserver_on_event = app_webserver_on_event,
    .cb_table = &aws_cb_table,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_WEBSERVER],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &network_tasks
    }
};


/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                               BUZZER                                                  */
/*====================================================================================================== */
void _on_buzzer_event(app_buzzer_event_t event, void * arg)
{

}

const app_buzzer_init_t _app_buzzer_init = 
{
    .fp_app_buzzer_on_event = _on_buzzer_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_BUZZER],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &hw_tasks
    }
};

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                                FUEL                                                   */
/*====================================================================================================== */
void app_fuel_on_event(app_fuel_event_t event, void * arg)
{    
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_fuel_event){
            _event_tables[i].on_fuel_event(event, arg);
        }
    }

    switch(event){
        case APP_FUEL_EVENT_PUMPING_STARTED:
            // LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Pumping started");
        break;
        
        case APP_FUEL_EVENT_PUMPING_STOPPED:
            // LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Pumping stopped");
        break;
        
        case APP_FUEL_EVENT_PUMPED:
        {            
            _app_fuel_event_count++;
            LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Pumped event count: %d", _app_fuel_event_count);
        }
        break;

        default:
        break;
    }
}

const app_fuel_init_t _app_fuel_init =
{
    .fp_app_fuel_on_event = app_fuel_on_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_FUEL],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &fuel_tasks
    }
};

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                           DISPLAY TAP                                                 */
/*====================================================================================================== */
void app_disptap_on_event(app_disptap_event_t event, void * arg)
{
    for(int i=0; i<_no_event_tables; i++)
    {
        if(_event_tables[i].on_ext_disptap_event)
        {
            _event_tables[i].on_ext_disptap_event(event, arg);
        }
    }

    switch(event)
    {
        case APP_ESP07_EVENT_FW_VERSION_LOADED:
            if(arg != NULL)
            {
                sprintf(_esp07_fw_version, "%s", (char *)arg);
                LOG_MSG_INFO(APP_DEB_LOG_EN, "ESP07 Firmware Version is %s", _esp07_fw_version);

                LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Configure OTA for ESP07 version checking");
                app_ota_on_driver_ready(ota_driver_idx_esp07dt);
            }
        break;

        default:
            // LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Unhandled ESP07 event: %d", (int)event);
        break;
    }
}

const app_disptap_init_t _app_disptap_init = 
{
    .fp_app_disptap_on_event = app_disptap_on_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_ESP07],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &fuel_tasks
    }
};

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                              OTA FTP                                                 */
/*====================================================================================================== */
void app_ota_on_event(app_ota_event_t event, void* arg)
{
    for(int i=0; i<_no_event_tables; i++)
    {
        if(_event_tables[i].on_ota_event)
        {
            _event_tables[i].on_ota_event(event, arg);
        }
    }

    switch (event) {       
        case APP_OTA_EVENT_DOWNLOAD_SUCCESS:
        {
            LOG_MSG_INFO(APP_DEB_LOG_EN, "OTA download and flash successful, rebooting...");
            
            ota_drver_list_idx_t driver_idx = (ota_drver_list_idx_t)(*(uint8_t *)arg);
            switch(driver_idx)
            {
                case ota_driver_idx_esp32main:
                    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Rebooting for ESP32 main firmware update");
                    pal_power_reset();
                    break;
                case ota_driver_idx_esp07dt:
                    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "ESP07 co-processor firmware update complete, no reboot needed");
                    pal_power_reset();
                    break;
                default:
                    LOG_MSG_ERROR(APP_DEB_LOG_EN, "Unknown driver index in OTA download success event: %d", driver_idx);
                    break;
            }
        }
        break;
        default:
        break;
    }
}

static const char* _ota_get_server_url()
{
    return "http://144.24.156.245:8080";
}

static const char* _ota_get_device_id()
{
    static char device_id[SIZE_OF_UUID] = {0};
    cube_sphere_get_device_uuid(device_id, SIZE_OF_UUID);
    return device_id;
}

static const char* _ota_get_firmware_type()
{
    return "fw-ferp-main-esp32-co-esp07";
}

static const char* _ota_get_current_version()
{
    return FW_VERSION;
}

static uint32_t _ota_get_timeout_ms()
{
    return 30000;
}

static const char* _ota_get_cert_pem()
{
    return NULL;  
}

static int32_t _fw_begin(pal_fw_update_handle_t* handle)
{
    return pal_fw_update_begin(handle);
}

static int32_t _fw_write(pal_fw_update_handle_t handle,
                         const uint8_t* data, size_t len)
{
    return pal_fw_update_write(handle, data, len);
}

static int32_t _fw_end(pal_fw_update_handle_t handle)
{
    return pal_fw_update_end(handle);
}

static int32_t _fw_abort(pal_fw_update_handle_t handle)
{
    return pal_fw_update_abort(handle);
}

static hsys_fw_update_driver_t _fw_update_driver = {
    .fp_begin = _fw_begin,
    .fp_write = _fw_write,
    .fp_end   = _fw_end,
    .fp_abort = _fw_abort,
};

static hsys_ota_driver_t _esp32main_ota_driver = {
    /* PAL implementation function pointers */
    .fp_check_version       = pal_esp_idf_ota_check_version,
    .fp_download_and_flash  = pal_esp_idf_ota_download_and_flash,
    /* Application event callback */
    .fp_on_event            = NULL,
    .event_ctx              = NULL,
    /* Flash writer driver */
    .fw_drv                 = &_fw_update_driver,
    /* Configuration getters */
    .fp_get_server_url      = _ota_get_server_url,
    .fp_get_device_id       = _ota_get_device_id,
    .fp_get_firmware_type   = _ota_get_firmware_type,
    .fp_get_current_version = _ota_get_current_version,
    .fp_get_timeout_ms      = _ota_get_timeout_ms,
    .fp_get_cert_pem        = _ota_get_cert_pem,
};

/* =========================================================================
 * ESP07 DT co-processor OTA driver
 *
 * Checks the OTA server for a "ferp-esp07-coprocessor" firmware image and
 * flashes it through the same pal_fw_update writer once an update is found.
 * The current version is read live from the co-processor via
 * _esp07dt_get_current_version() — replace the stub below with the real
 * call once the co-processor version query is implemented.
 * ========================================================================= */

static const char* _esp07dt_get_firmware_type()
{
    return "fw-ferp-co-esp07";
}

static const char* _esp07dt_get_current_version()
{
    return (const char *)&_esp07_fw_version;
}

static uint32_t _esp07dt_get_timeout_ms()
{
    return 60000;
}

static const char* _esp07dt_get_cert_pem()
{
    return NULL;
}

static int32_t _esp07dt_fw_begin(pal_fw_update_handle_t* handle)
{
    int32_t ret;
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Upload Start: %s", "rtos_dis_tap_esp07.bin");        
    sprintf((char *)esp07_ota_file_download_meta.file_path, "/esp07/%s", "rtos_dis_tap_esp07.bin");

    ret = app_spiffs_delete_file((const char *)esp07_ota_file_download_meta.file_path, 1000);
    if(ret != ERROR_OK)
    {
        LOG_MSG_ERROR(APP_DEB_LOG_EN, "Failed to delete existing file: %s", esp07_ota_file_download_meta.file_path);
        return ERROR_FAILED;
    }

    ret = app_spiffs_create_file((const char *)esp07_ota_file_download_meta.file_path, 1000);
    if(ret != ERROR_OK)
    {
        LOG_MSG_ERROR(APP_DEB_LOG_EN, "Failed to create file: %s", esp07_ota_file_download_meta.file_path);
        return ERROR_FAILED;
    }

    esp07_ota_file_download_meta.crc32 = 0;
    esp07_ota_file_download_meta.file_size = 0;
    esp07_ota_file_download_meta.state = esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_IDLE;

    return ERROR_OK;

}

static int32_t _esp07dt_fw_write(pal_fw_update_handle_t handle,
                         const uint8_t* data, size_t len)
{
    int32_t ret;
	
    ret = app_spiffs_append_file((const char *)esp07_ota_file_download_meta.file_path, (const uint8_t *)data, len, 2000);
    if(ret != ERROR_OK)
    {
        LOG_MSG_ERROR(APP_DEB_LOG_EN, "Failed to append data to file: %s", esp07_ota_file_download_meta.file_path);
        return ERROR_FAILED;
    }        
    esp07_ota_file_download_meta.crc32 = crc32_update(esp07_ota_file_download_meta.crc32, data, len);
    esp07_ota_file_download_meta.file_size += len;
    esp07_ota_file_download_meta.state = esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_INPROGRESS;
    return ERROR_OK;
}

static int32_t _esp07dt_fw_end(pal_fw_update_handle_t handle)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "File %s received successfully, size: %d bytes", "rtos_dis_tap_esp07.bin", esp07_ota_file_download_meta.file_size);
    esp07_ota_file_download_meta.state = esp07_ota_file_download_meta.ESP07_OTA_FILE_DOWNLOAD_FINISHED;

    return ERROR_OK;
}

static int32_t _esp07dt_fw_abort(pal_fw_update_handle_t handle)
{
    return pal_fw_update_abort(handle);
}

static hsys_fw_update_driver_t _esp07dt_update_driver = {
    .fp_begin = _esp07dt_fw_begin,
    .fp_write = _esp07dt_fw_write,
    .fp_end   = _esp07dt_fw_end,
    .fp_abort = _esp07dt_fw_abort,
};

static hsys_ota_driver_t _esp07dt_ota_driver = {
    /* PAL implementation function pointers */
    .fp_check_version       = pal_esp_idf_ota_check_version,
    .fp_download_and_flash  = pal_esp_idf_ota_download_and_flash,
    /* Application event callback — reuse the same handler */
    .fp_on_event            = NULL,
    .event_ctx              = NULL,
    /* Flash writer driver — same pal_fw_update path */
    .fw_drv                 = &_esp07dt_update_driver,
    /* Configuration getters */
    .fp_get_server_url      = _ota_get_server_url,
    .fp_get_device_id       = _ota_get_device_id,
    .fp_get_firmware_type   = _esp07dt_get_firmware_type,
    .fp_get_current_version = _esp07dt_get_current_version,
    .fp_get_timeout_ms      = _esp07dt_get_timeout_ms,
    .fp_get_cert_pem        = _esp07dt_get_cert_pem,
};

/* =========================================================================
 * OTA module init — pass both drivers as an array
 * ========================================================================= */

static hsys_ota_driver_t* _ota_drivers[ota_driver_idx_count] = {
    &_esp32main_ota_driver,
    &_esp07dt_ota_driver
};

const app_ota_init_t _app_ota_init =
{
    .fp_app_ota_on_event = app_ota_on_event,
    .drivers      = _ota_drivers,
    .driver_count = ota_driver_idx_count,
    .app_init =
    {
        .event_table  = &_event_tables[APP_ID_OTA],
        .fp_wake      = hsys_taskrunner::wake_trampoline,
        .wake_context = &network_tasks
    }
};

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                           TIME MANAGER                                                */
/*====================================================================================================== */
void app_time_on_event(app_timeman_event_t event, void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "on time event %d", (int)event);
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_timeman_event){
            _event_tables[i].on_timeman_event(event, arg);
        }
    }
      
    if(arg != NULL){
        time_t * new_time = (time_t *)arg;
        LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Time event with time: %ld", *new_time);
        
        // Get current system time
        time_t current_time = time(NULL);
        
        // Only update if the new time is in the future
        if(*new_time > current_time){
            LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Updating system time from %ld to %ld", current_time, *new_time);
            
            // Update system time using PAL
            if(pal_time_set_epoch_time(*new_time) < 0){
                LOG_MSG_ERROR(APP_DEB_LOG_EN, "Failed to set system time");
            } else {
                LOG_MSG_DEBUG(APP_DEB_LOG_EN, "System time updated successfully");
            }
        } else {
            LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Received time (%ld) is not newer than current time (%ld), ignoring", 
                            *new_time, current_time);
        }
    }    
}

const hsys_spiffs_t spiffs = 
{
    .fp_write = app_spiffs_write,
    .fp_read = app_spiffs_read
};

const timeman_configs_t _timeman_configs = 
{
    .local_backup_filepath = "timeman_backup.json",
    .spiffs = &spiffs,
    .rtc = &ds1307_rtc,
    .ntp = &ntp_default,
};

const timeman_init_t _app_timeman_init = 
{
    .p_configs = &_timeman_configs,
    .fp_app_timeman_on_event = app_time_on_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_TIMING],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &base_tasks
    }
};

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                              HARDWARE                                                 */
/*====================================================================================================== */
void app_hw_on_event(app_hw_event_t event, void * arg)
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "on hw event %d", (int)event);
    for(int i=0; i<_no_event_tables; i++){
        if(_event_tables[i].on_hw_event){
            _event_tables[i].on_hw_event(event, arg);
        }
    }
}

const app_hw_init_t _app_hw_init = 
{
    .fp_app_hw_on_event = app_hw_on_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_HW],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &hw_tasks
    }
};



/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                                MQTT                                                   */
/*====================================================================================================== */

void app_mqtt_on_event(app_mqtt_event_t event, void * arg)
{
    LOG_MSG_DEBUG(LOG_EN, "Mqtt event: %d", (int)event);
    for(int i=0; i<_no_event_tables; i++)
    {
        if(_event_tables[i].on_mqtt_event)
        {
            _event_tables[i].on_mqtt_event(event, arg);
        }
    }
}

#include "ferp_frame_v1.h"
#include "base64.hpp"

#define OTA_BUFFER_SIZE 1024
static uint8_t mqtt_process_buffer[OTA_BUFFER_SIZE] = {0};

static uint32_t _ota_offset_expecting = 0;
int32_t app_mqtt_on_command(const uint8_t * payload, uint16_t payload_len)
{
    if (!payload || payload_len == 0) 
    {
        return ERROR_FAILED;
    }

    size_t requiredSize = Base64Decoder::calculateDecodedSize(payload_len);
    if(OTA_BUFFER_SIZE < requiredSize)
    {
        LOG_MSG_ERROR(LOG_EN, "Buffer size is not enough");
        return ERROR_APP_BUSY;
    }

    size_t mqtt_process_buffer_size = 0;

    bool is_decoded = Base64Decoder::decode((const char *)payload, payload_len, mqtt_process_buffer, mqtt_process_buffer_size);
    if(!is_decoded) 
    {
        LOG_MSG_ERROR(LOG_EN, "Failed to decode base64");
        return ERROR_FAILED;
    }
    
    static HsysCmdRespFrame_t frame;
    if (!hsys_frame_init(&frame)) 
    {
        LOG_MSG_ERROR(LOG_EN, "Failed to initialize frame");
        return ERROR_FAILED;
    }

    LOG_MSG_DEBUG(LOG_EN, "RX Size = %ld", mqtt_process_buffer_size);
    if (hsys_frame_deserialize(&frame, mqtt_process_buffer, mqtt_process_buffer_size)) 
    {
        // LOG_MSG_DEBUG(LOG_EN, "Message processed successfully");
        // hsys_print_frame(&frame);
    } 
    else 
    {
        LOG_MSG_ERROR(LOG_EN, "Failed to parse incoming message");
    }

    switch(frame.commandId)
    {
        static uint32_t _esp32bin_size = 0;
        static bool is_ota_in_progress = false;
        static uint32_t ota_start_ts = 0;
        static bool is_ota_started_with_aws = false;
        static bool is_ota_starated_with_mqtt = false;
        static bool is_ota_started_with_mqtt = false;

        case CMD_OTA_START:
        {
            LOG_MSG_DEBUG(LOG_EN, "OTA Start Command Received");
            HsysOtaId_t otaId;
            uint32_t otaSize;

            if(is_ota_in_progress && (pal_time_get_ms() - ota_start_ts < OTA_TIMEOUT))
            {
                LOG_MSG_DEBUG(LOG_EN, "OTA in progress");
                frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_BUSY);                
            }

            if(hsys_parse_ota_start_command_data(frame.data, frame.dataSize, &otaId, &otaSize))
            {
                LOG_MSG_DEBUG(LOG_EN, "OTA ID: %d, OTA Size: %d", otaId, otaSize);
                frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_OK);
                
                // if(Update.begin(UPDATE_SIZE_UNKNOWN))
                // {                    
                //     _esp32bin_size = otaSize;
                //     _ota_offset_expecting = 0;
                //     is_ota_in_progress = true;
                //     ota_start_ts = millis();
                //     is_ota_started_with_mqtt = true;

                //     LOG_MSG_DEBUG(LOG_EN, "OTA Update Started, update size %ld", Update.size());
                // }
                // else
                // {
                //     Update.abort();
                //     LOG_MSG_ERROR(LOG_EN, "Update begin failed : %s", Update.errorString());
                //     frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_INVALID_DATA);
                // }
            }
            else
            {
                LOG_MSG_ERROR(LOG_EN, "Failed to parse OTA Start command data");
                frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_INVALID_DATA);
            }
        }
        break;

        case CMD_OTA_DATA:
        {
            if(!is_ota_in_progress || !is_ota_started_with_mqtt)
            {
                LOG_MSG_DEBUG(LOG_EN, "OTA not in progress or not started with MQTT");
                frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_INVALID_DATA);
                break;
            }

            // LOG_MSG_DEBUG(LOG_EN, "OTA Data Command Received");
            
            // bool hsys_parse_ota_data_command_data(const uint8_t* data, uint32_t size, 
                                    //  uint32_t* otaOffset, uint8_t* otaDataBuffer, uint16_t* dataSize) 
            uint16_t ota_chunk_size = 0;
            uint32_t ota_offset = 0;
            uint8_t ota_buffer[frame.dataSize];
            if(hsys_parse_ota_data_command_data(frame.data, frame.dataSize, &ota_offset, ota_buffer, &ota_chunk_size))
            {                
                if(_ota_offset_expecting != ota_offset)
                {
                    LOG_MSG_ERROR(LOG_EN, "OTA Offset Mismatch, Expected: %d, Received: %d", _ota_offset_expecting, ota_offset);
                    frame.dataSize = hsys_create_ota_data_response_data(frame.data, _ota_offset_expecting, CMD_STATUS_INVALID_DATA);
                    break;
                }

                // if(Update.write(ota_buffer, ota_chunk_size) != ota_chunk_size) 
                // {
                //     LOG_MSG_ERROR(LOG_EN, "Update Write failed %s", Update.errorString());
                //     frame.dataSize = hsys_create_ota_data_response_data(frame.data, _ota_offset_expecting, CMD_STATUS_OTA_ERROR);
                //     break;
                // }

                _ota_offset_expecting = ota_offset + ota_chunk_size;

                LOG_MSG_DEBUG(LOG_EN, "OTA Next Offset %ld OTA Offset: %ld, OTA Chunk Size: %ld", _ota_offset_expecting, ota_offset, ota_chunk_size);
                frame.dataSize = hsys_create_ota_data_response_data(frame.data, _ota_offset_expecting, CMD_STATUS_OK);
            }
            else
            {
                LOG_MSG_ERROR(LOG_EN, "Failed to parse OTA Data command data");
                frame.dataSize = hsys_create_ota_data_response_data(frame.data, _ota_offset_expecting, CMD_STATUS_INVALID_DATA);
            }
        }
        break;

        case CMD_OTA_COMPLETE:
        {
            LOG_MSG_DEBUG(LOG_EN, "OTA End Command Received");
            // LOG_MSG_DEBUG(LOG_EN, "OTA Size Expected: %ld, OTA Size Received: %ld", _esp32bin_size, Update.size());

            // if (Update.end(true)) 
            // { // true to set the size to the current progress
            //     LOG_MSG_DEBUG(LOG_EN, "OTA End Success");
            //     ota_state = OTA_STATE_END;

            //     is_ota_in_progress = false;
            //     is_ota_started_with_mqtt = false;
            // } 
            // else 
            // {
            //     LOG_MSG_ERROR(LOG_EN, "Update End failed %s", Update.errorString());
            //     ota_state = OTA_STATE_IDLE;
            // }
            frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_OK);
            // Handle OTA end command
        }
        break;

        case CMD_OTA_GET_STATUS:
            LOG_MSG_DEBUG(LOG_EN, "OTA Get Status Command Received");
            frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_OK);
            // Handle OTA get status command
        break;
        case CMD_GET_FW_VERSION:
            LOG_MSG_DEBUG(LOG_EN, "Get Firmware Version Command Received");
            frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_OK);
            // Handle Get Firmware Version command
        break;
        case CMD_GET_FW_VERSION_SUB_1:
            LOG_MSG_DEBUG(LOG_EN, "Get Firmware Version Sub 1 Command Received");
            frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_OK);
            // Handle Get Firmware Version Sub 1 command
        break;
        default:            
            frame.dataSize = hsys_create_basic_response_data(frame.data, CMD_STATUS_OK);
            return ERROR_FAILED;
        break;
    }    

    uint32_t resp_frame_size = 0;
    bool is_ready = hsys_frame_serialize(&frame, mqtt_process_buffer, mqtt_process_buffer_size, &resp_frame_size);
    if(!is_ready)
    {
        LOG_MSG_ERROR(LOG_EN, "Failed to serialize response frame");
        return ERROR_FAILED;
    }

    // LOG_DEBUG_BUFFER("TX: Payload: ", frame.data, frame.dataSize);
    // LOG_MSG_DEBUG(LOG_EN, "Response Size: %d", resp_frame_size);
    // LOG_DEBUG_BUFFER("TX: ", mqtt_process_buffer, resp_frame_size);

    size_t encoded_size = Base64Encoder::calculateEncodedSize(resp_frame_size);
    uint8_t base64_encoded[encoded_size+1];

    // LOG_MSG_DEBUG(LOG_EN, "Encoded Size: %d", encoded_size);

    Base64Encoder::encode(mqtt_process_buffer, resp_frame_size, (char *)base64_encoded, encoded_size);
    // LOG_MSG_DEBUG(LOG_EN, "TX Encoded : %s", (char *)base64_encoded);

    char uuid[SIZE_OF_UUID] = {0};  
    char topic[SIZE_OF_BASE_TOPIC + SIZE_OF_MQTT_NOTIF_TOPIC] = {0};
    uint8_t mac_address[8]; 

    board_get_board_id(uuid, mac_address, 8);
    char response_pipe_id_str[10] = {0};
    hsys_frame_get_response_pipe_id(&frame, response_pipe_id_str);
    sprintf(topic, "%s/v1/dev/response/%s/%s", "hsys",  uuid, response_pipe_id_str);
    app_mqtt_publish(topic, 0, (const char *)base64_encoded);

    return ERROR_FAILED;
}

#define APP_MQTT_NO_SUB_TOPICS              (1)
const char * get_app_mqtt_sub_topic(uint8_t idx, char * topic, fp_on_mqtt_msg_t * callback)
{
    if (idx < APP_MQTT_NO_SUB_TOPICS)
    {
        uint8_t mac_address[8];  
        char uuid[SIZE_OF_UUID] = {0};  
        board_get_board_id(uuid, mac_address, 8);

        switch(idx)
        {
            case 0:
                sprintf(topic, "%s/v1/dev/command/%s", "hsys",  uuid);
                *callback = app_mqtt_on_command;
                LOG_MSG_DEBUG(LOG_EN, "Subscribing to %s", topic);  
                return topic; 
            break;    
            default:
                return NULL;
            break;
        }   
    }

    return NULL;
}

int32_t app_mqtt_get_config(pal_mqtt_config_t * mqtt_init, uint32_t timeout_ms)
{
    int32_t ret = ERROR_APP_INVALID_ARGUMENTS;

    if(mqtt_init == nullptr){
        return ret;
    }

    memset(mqtt_init, 0, sizeof(pal_mqtt_config_t));

    // Broker URI — build "mqtt://<host>" from stored host config
    snprintf(mqtt_init->broker_uri, PAL_MQTT_MAX_BROKER_URI_LEN,
             "mqtt://%.*s",
             (int)(PAL_MQTT_MAX_BROKER_URI_LEN - (int)sizeof("mqtt://")),
             _app_config.mqtt.broker_uri);

    // Port
    mqtt_init->port = _app_config.mqtt.port;

    // Transport — plain TCP (no SSL/TLS)
    mqtt_init->transport = PAL_MQTT_TRANSPORT_OVER_TCP;

    // Client ID — generated from board UUID so each device is unique
    char uuid[SIZE_OF_UUID] = {0};
    uint8_t mac_address[8] = {0};
    board_get_board_id(uuid, mac_address, sizeof(mac_address));
    snprintf(mqtt_init->client_id, PAL_MQTT_MAX_CLIENT_ID_LEN, "%s", uuid);

    // No username / password
    mqtt_init->username[0] = '\0';
    mqtt_init->password[0] = '\0';

    // No TLS certificates
    mqtt_init->cert_pem                  = NULL;
    mqtt_init->client_cert_pem           = NULL;
    mqtt_init->client_key_pem            = NULL;
    mqtt_init->skip_cert_common_name_check = false;

    ret = ERROR_OK;
    return ret;
}

const app_mqtt_init_t _app_mqtt_init = 
{
    .fp_app_mqtt_get_config = app_mqtt_get_config,
    .fp_app_mqtt_on_event = app_mqtt_on_event, 
    .fp_get_mqtt_sub_topic = get_app_mqtt_sub_topic,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_MQTT],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &network_tasks
    },
};


/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                               LEDS                                                    */
/*====================================================================================================== */
void app_led_on_event(app_led_event_t event, void * arg)
{

}

app_led_init_t _app_led_init = 
{
    .fp_app_led_on_event = app_led_on_event,
    .app_init = 
    {
        .event_table = &_event_tables[APP_ID_LED],
        .fp_wake = hsys_taskrunner::wake_trampoline,
        .wake_context = &hw_tasks
    },
};

/*====================================================================================================== */
/*=================================================.==================================================== */
/*                                            APPLICATION                                                */
/*====================================================================================================== */
void app_init()
{
	board_ini();

    LOG_MSG_INFO(LOG_EN, "Starting application...");

    pal_fw_update_print_bin_info(PAL_BIN_SLOT_RUNNING);
    pal_fw_update_print_bin_info(PAL_BIN_SLOT_NEXT);

    _app_config_load_default();

    hsys_taskrunner::init();  

    app_config_init(&_app_config_init);  
    app_wifi_init(&_app_wifi_init);
    app_cloud_init(&_app_cloud_init);
    app_webserver_init(&_app_webserver_init);
    app_fuel_init(&_app_fuel_init);
    app_disptap_init(&_app_disptap_init);
    app_print_btn_init(&_app_print_btn_init);
    app_default_btn_init(&_app_default_btn_init);
    app_hw_init(&_app_hw_init);
    app_timeman_init(&_app_timeman_init);
    app_retransmit_init(&_app_retransmit_init);
    app_internet_init(&_app_internet_init);
    app_mqtt_init(&_app_mqtt_init);
    app_buzzer_init(&_app_buzzer_init);    
    app_ota_init(&_app_ota_init);
    app_led_init(&_app_led_init);

    app_sd_init(app_sd_on_event, &_event_tables[APP_ID_SD], 5);
    app_spiffs_init(app_spiffs_on_event, &_event_tables[APP_ID_SPIFFS], 5);
}

void app_run()
{
    LOG_MSG_DEBUG(APP_DEB_LOG_EN, "Running application...");
    hsys_task_delay(20000);

}
