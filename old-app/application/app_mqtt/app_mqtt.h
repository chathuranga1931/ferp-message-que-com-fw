#ifndef __APP_MQTT_H__
#define __APP_MQTT_H__

#include <stdint.h>
#include "app_common.h"
#include "user_config.h"

typedef int32_t (*fp_on_mqtt_msg_t)(const uint8_t * payload, uint16_t payload_len);
typedef const char * (*fp_get_mqtt_sub_topic_t)(uint8_t idx, char * path, fp_on_mqtt_msg_t * callback);
// typedef struct {
//     // fp_app_mqtt_on_event_t fp_app_mqtt_on_event;
//     // const app_mqtt_request_t * p_request_table;
//     // uint16_t no_request_table_entries;
//     fp_get_mqtt_sub_topic_t fp_get_mqtt_sub_topic;
//     // int32_t (*fp_app_mqtt_set_config_cb)(const char * config, const void * value, uint16_t bytes, hsys_type_t type, uint32_t timeout_ms);
//     // int32_t (*fp_app_mqtt_get_config_cb)(const char * config, void * value, uint16_t * pBytes, hsys_type_t * pType, uint32_t timeout_ms);    
//     app_init_t app_init;
// }app_mqtt_init_t;

// MQTT RX message structure
// typedef struct {
//     char topic[USER_CONFIG_MAX_MQTT_TOPIC_LENGTH];
//     uint8_t * payload;
//     uint16_t payload_len;
// }app_mqtt_rx_message_t;

typedef struct {    
    char    topic[USER_CONFIG_MAX_MQTT_TOPIC_LENGTH]; /**< Null-terminated topic string  */
    uint8_t * payload;                               /**< Pointer into static rx_payload_buf */
    uint8_t qos;
    bool dup;
    bool retain;
    size_t len;
    size_t index;
    size_t total;
}app_mqtt_rx_message_t;

// MQTT topic structure
typedef struct {
    char topic[APP_MQTT_SUBTOPIC_MAX_SIZE];
    uint8_t qos;
    char path[APP_MQTT_SUBTOPIC_MAX_SIZE + APP_MQTT_SUBTOPIC_MAX_SIZE];
}mqtt_topic_t;

#include "pal_mqtt.h"
typedef pal_mqtt_event_t app_mqtt_event_t;

typedef void (*fp_app_mqtt_on_event_t)(app_mqtt_event_t event, void * arg);
typedef int32_t (*fp_app_mqtt_get_config_t)(pal_mqtt_config_t * mqtt_init, uint32_t timeout_ms);

// MQTT initialization structure
typedef struct {
    fp_app_mqtt_get_config_t fp_app_mqtt_get_config;
    fp_app_mqtt_on_event_t fp_app_mqtt_on_event;
    fp_get_mqtt_sub_topic_t fp_get_mqtt_sub_topic;
    app_init_t app_init;
}app_mqtt_init_t;

// Function declarations
void app_mqtt_init(const app_mqtt_init_t * p_mqtt_init);
void app_mqtt_run();
int32_t app_mqtt_publish(const char * topic, const char * payload, uint16_t payload_len, uint8_t qos, bool retain);
bool app_mqtt_is_connected();

typedef enum{
    APP_MQTT_REQUEST_MSG_SET_DEVICE,
    APP_MQTT_REQUEST_MSG_SET_APP,
    APP_MQTT_REQUEST_MSG_SET_4TO20MA_ADC,
    APP_MQTT_REQUEST_MSG_SET_4TO20MA_SENSOR,

    APP_MQTT_REQUEST_MSG_GET_DEVICE,
    APP_MQTT_REQUEST_MSG_GET_APP,
    APP_MQTT_REQUEST_MSG_GET_4TO20MA_ADC,
    APP_MQTT_REQUEST_MSG_GET_4TO20MA_SENSOR,

    APP_MQTT_REQUEST_MSG_GET_SNSR_DT,

    APP_MQTT_REQUEST_MSG_SET_SERL_LOG_LVL,
    APP_MQTT_REQUEST_MSG_GET_SERL_LOG_LVL,
    
    APP_MQTT_REQUEST_MSG_ENABLE_SENSOR_LOGS,
    APP_MQTT_REQUEST_MSG_REBOOT,

    APP_MQTT_NO_REQUEST_MSG_TYPES,
    APP_MQTT_REQUEST_MSG_NONE,
}app_mqtt_request_msg_type_t;

// const std::string _request_types[APP_MQTT_NO_REQUEST_MSG_TYPES] = {
//     "set_config_device",
//     "set_config_app",
//     "set_config_4to20ma_adc",
//     "set_config_4to20ma_sensor",

//     "get_config_device",
//     "get_config_app",
//     "get_config_4to20ma_adc",
//     "get_config_4to20ma_sensor",

//     "get_snsr_dt",
//     "set_serl_log_lvl",
//     "get_serl_log_lvl",

//     "enable_sensor_raw_logs",
//     "reboot"
// };


typedef int32_t (*fp_on_request)(const char * payload, uint16_t payload_len, char *response_payload);
#define MAX_JSON_NAME_LEN   32  // Adjust as needed

typedef struct {
    char request_json_name[MAX_JSON_NAME_LEN];
    fp_on_request on_request;
    uint32_t expected_response_max_bytes;
}app_mqtt_request_t;

int32_t app_mqtt_on_request(const char * payload, uint8_t * response_payload, uint32_t bytes);
void app_mqtt_run();
int32_t app_mqtt_publish(const char * topic, uint8_t qos, const char * payload);

#endif 