
#ifndef __APP_H__
#define __APP_H__

#include "app_wifi.h"
#include "app_config.h"


#include "hsys_rtc.h"
#include "pal_ntp.h"







// typedef struct{
//     uint32_t port;
//     uint32_t host_mode; //URL or IP
//     char host[SIZE_OF_MQTT_HOST];
//     char subscribe[SIZE_OF_MQTT_TOPIC];
//     char publish[SIZE_OF_MQTT_TOPIC];
// }app_mqtt_config_t;

// typedef struct{
//     std::string mac;
//     hs_string_16_t fw_ver;
//     hs_string_32_t board_uuid;
//     hs_string_32_t board_name;
//     hs_string_32_t hw_ver;
//     hs_string_32_t device_type;  
// }app_device_t;






typedef enum {

}app_logging_event_t;


typedef enum {

}app_printer_event_t;








typedef enum{    
}app_timing_event_t;

typedef enum{

}app_screen_event_t;


typedef enum{
    APP_PUMP_STARTED,
    APP_PUMP_STOPPED,
}app_pump_event_t;


typedef enum{
    APP_SPIFFS_EVNT_SPIFFS_READY,
}app_spiffs_event_t;

typedef enum {
    APP_SD_EVNT_SD_READY,
}app_sd_event_t;


typedef enum {
    APP_SYS_EVENT_REBOOT_SCHEDULED_IN_X_SECONDS = 0,
}app_sys_event_t;

// typedef void (*fp_app_pump_on_event_t)(app_pump_event_t event, void * arg);
// typedef void (*fp_app_ota_on_event_t)(app_ota_event_t event, void * arg);
typedef void (*fp_app_logging_on_event_t)(app_logging_event_t event, void * arg);
typedef void (*fp_app_printer_on_event_t)(app_printer_event_t event, void * arg);
typedef void (*fp_app_timing_on_event_t)(app_timing_event_t event, void * arg);
typedef void (*fp_app_screen_on_event_t)(app_screen_event_t event, void * arg);
typedef void (*fp_app_spiffs_on_event_t)(app_spiffs_event_t event, void * arg);
typedef void (*fp_app_sd_on_event_t)(app_sd_event_t event, void * arg);
typedef void (*fp_app_sys_on_event_t)(app_sys_event_t event, void * arg);
// typedef void (*fp_app_timeman_on_event_t)(app_timeman_event_t event, void * arg);

int32_t app_sd_write_file(const char* file_path, const char* content, uint32_t timeout_ms);
int32_t app_sd_read_file(const char* file_path, char* content, size_t max_len, uint32_t timeout_ms);
int32_t app_sd_delete_file(const char* file_path, uint32_t timeout_ms);
int32_t app_sd_create_file(const char* file_path, uint32_t timeout_ms);
int32_t app_sd_append_line(const char* file_path, const char* line, uint32_t timeout_ms);
int32_t app_sd_read_line(const char * path, uint32_t line, char * read_line, size_t max_len, uint32_t timeout_ms);
int32_t app_sd_remove_dir(const char * path, uint32_t timeout_ms);














// typedef struct {    
//     typedef void (*fp_on_)(app_internet_event_t event, void * arg);
// }ota_handler_t;


typedef struct {
    const char * filepath;
    const char * content;
    size_t * content_size;
    uint32_t timeout_ms;
    char mode[4]; // e.g., "r", "w", "a"
} file_operation_config_t;

typedef struct {
    bool (*fp_write)(file_operation_config_t * config);
    bool (*fp_read)(file_operation_config_t * config);
}hsys_spiffs_t;

// typedef struct {
//     const char * local_backup_filepath;
//     const hsys_spiffs_t * spiffs;
//     const hsys_rtc_t * rtc;
//     // const hsys_ntp_t * ntp;
// }timeman_configs_t;

// typedef struct {
//     const timeman_configs_t * p_configs;
//     fp_app_timeman_on_event_t fp_app_timeman_on_event;
//     app_init_t app_init;
// }timeman_init_t;


// void app_ota_init(fp_app_ota_on_event_t fp_app_ota_on_event, event_table_t * event_table, int32_t priority);
void app_logging_init(fp_app_logging_on_event_t fp_app_logging_on_event, event_table_t * event_table, int32_t priority);
void app_printer_init(fp_app_printer_on_event_t fp_app_printer_on_event, event_table_t * event_table, int32_t priority);


void app_timing_init(fp_app_timing_on_event_t fp_app_timing_on_event, event_table_t * event_table, int32_t priority);
void app_screen_init(fp_app_screen_on_event_t fp_app_screen_on_event, event_table_t * event_table, int32_t priority);
void app_spiffs_init(fp_app_spiffs_on_event_t fp_app_spiffs_on_event, event_table_t * event_table, int32_t priority);
void app_sd_init(fp_app_sd_on_event_t fp_app_sd_on_event, event_table_t * event_table, int32_t priority);
void app_sys_init(fp_app_sys_on_event_t fp_app_sys_on_event, event_table_t * event_table, int32_t priority);

int32_t app_spiffs_append_line(const char * file_path, const char * line, uint32_t timeout_ms);
int32_t app_spiffs_create_file(const char * file_path, uint32_t timeout_ms);
int32_t app_spiffs_delete_file(const char * file_path, uint32_t timeout_ms);
int32_t app_spiffs_write_file(const char * file_path, const char * content, uint32_t timeout_ms);
int32_t app_spiffs_read_file(const char * file_path, char * content, uint32_t * read_size, uint32_t timeout_ms);


// const app_config_t * app_config_get();



void app_init();
void app_run();


// int32_t app_timeman_init(const timeman_init_t * p_timeman_init);
// int32_t timeman_get_last_working(time_t * time);
// int32_t timeman_update_last_working_time(void);
// void timeman_run(void);


bool app_spiffs_read(file_operation_config_t* config);
bool app_spiffs_write(file_operation_config_t * config);
int32_t app_spiffs_append_file(const char * file_path, const uint8_t * data, size_t len, uint32_t timeout_ms);

// bool hsys_spiffs_write(file_operation_config_t * config);
// bool hsys_spiffs_read(file_operation_config_t * config);

#endif /* _APP_H_ */