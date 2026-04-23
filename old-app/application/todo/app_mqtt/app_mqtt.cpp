
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ArduinoJson.h>

#include "pal_mqtt.h"
#include "pal_logger.h"

#include "hsys_task.h"
#include "hsys_event.h"
#include "hsys_queue.h"
#include "hsys_mutex.h"
#include "hsys_soft_timer.h"

#include "board.h"

#include "app.h"
#include "app_mqtt.h"
#include "app_wifi/app_wifi.h"
#include "app_config/app_config.h"
#include "app_internet/app_internet.h"

#define __TAG__  "APP_MQTT"

#define MQTT_APP_EVENT_CONFIG_READY                     (0x1<<0)
#define MQTT_APP_EVENT_WIFI_CONNECTED                   (0x1<<1)
#define MQTT_APP_EVENT_WIFI_DISCONNECTED                (0x1<<2)
#define MQTT_APP_EVENT_INTERNET_CONNECTED               (0x1<<3)
#define MQTT_APP_EVENT_INTERNET_DISCONNECTED            (0x1<<4)
#define MQTT_APP_EVENT_MQTT_CONNECTED                   (0x1<<5)
#define MQTT_APP_EVENT_MQTT_DISCONNECTED                (0x1<<6)
#define MQTT_APP_EVENT_MQTT_MSG_RCVD                    (0x1<<7)
#define MQTT_APP_EVENT_MQTT_MSG_BUSY                    (0x1<<8)
#define MQTT_APP_EVENT_MESSAGE_RECEIVED                 (0x1<<9)
#define MQTT_APP_EVENT_MQTT_RECONNECT_REQUIRED         (0x1<<10)


// #define MQTT_APPSENSOR_DATA_QUEUE_LENGTH            (5)
// #define MQTT_APPSENSOR_DATA_QUEUE_ITEM_SIZE         (sizeof(app_mqtt_sensor_data_t))
// #define MQTT_APPDEVINFO_DATA_QUEUE_LENGTH           (1)
// #define MQTT_APPDEVINFO_DATA_QUEUE_ITEM_SIZE        (sizeof(app_mqtt_device_info_t))

// typedef enum{    
//     APP_MQTT_TOPIC_REQUEST_THIS_DEVICE,
//     APP_MQTT_TOPIC_REQUEST_DEVICE_TYPE,
//     APP_MQTT_TOPIC_REQUEST_DEVICE_GROUP,
//     APP_MQTT_TOPIC_REQUEST_DEVICE_TYPE_DEVICE_GROUP,

//     APP_MQTT_NO_SUBSCRIBED_TOPICS,
//     APP_MQTT_TOPIC_CONFIG_NONE,
// }app_mqtt_sub_topic_t;

// typedef enum{
//     APP_MQTT_TOPIC_PUBLISH_EVENT_THIS_DEVICE,
//     APP_MQTT_TOPIC_PUBLISH_STARTUP_THIS_DEVICE,
//     APP_MQTT_TOPIC_PUBLISH_RECON_THIS_DEVICE,
//     APP_MQTT_TOPIC_PUBLISH_RESPONSE_THIS_DEVICE,
//     APP_MQTT_NO_PUBLISHED_TOPICS,
// }app_mqtt_pub_topic_t;


// typedef struct {    
//     char topic[USER_CONFIG_MAX_MQTT_TOPIC_LENGTH];
//     char payload[USER_CONFIG_MAX_MQTT_PAYLOAD_LENGTH];
//     uint8_t qos;
//     bool dup;
//     bool retain;
//     size_t len;
//     size_t index;
//     size_t total;
// }app_mqtt_rx_message_t;


// static device_t * _device;
static bool _is_initialized = false;
static hsys_task_handle_t _app_mqtt_main_task_handle __attribute__((unused));
static hsys_eventgroup_handle_t _mqtt_events;
static hsys_queue_handle_t _mqtt_appsensor_data_queue __attribute__((unused));
static hsys_queue_handle_t _mqtt_deviceinfo_data_queue __attribute__((unused));
static pal_mqtt_client_handle_t _mqtt_client = NULL;
static hsys_timer_handle_t _mqtt_appsensor_data_periodic_timer __attribute__((unused));

const app_mqtt_request_t * _p_app_mqtt_request_table = NULL;
uint16_t _app_mqtt_request_table_size = 0;

// typedef struct {
//     char path[APP_MQTT_SUBTOPIC_MAX_SIZE + APP_MQTT_SUBTOPIC_MAX_SIZE];
// }mqtt_topic_t;

// mqtt_topic_t _mqtt_topics_subscribed[APP_MQTT_NO_SUBSCRIBED_TOPICS];
// mqtt_topic_t _mqtt_topics_published[APP_MQTT_NO_PUBLISHED_TOPICS];

static mqtt_topic_t temp_mqtt_topic __attribute__((unused));
static bool _is_wifi_connected = true;

// static std::string _device_group = "none"; 
// static std::string _device_type = "none";
// static std::string _device_mac = "none";

// static int8_t _wifi_rssi;
// char _wifi_ip_address[SIZE_OF_IP_ADDRESS];
// char _wifi_ssid[SIZE_OF_WIFI_SSID];



// static uint32_t _mqtt_msg_idx = 0;
// String _json_string_cmd;

// std::string _topics_subscribed[APP_MQTT_NO_SUBSCRIBED_TOPICS];
// std::string _topics_published[APP_MQTT_NO_PUBLISHED_TOPICS];

hsys_mutex_handle_t _app_mqtt_rx_message_mutex;
// Dedicated static buffer for incoming MQTT payload — the rx_message struct
// holds a pointer into this buffer instead of an embedded array, keeping the
// struct small regardless of USER_CONFIG_MAX_MQTT_PAYLOAD_LENGTH.
static uint8_t _mqtt_rx_payload_buf[USER_CONFIG_MAX_MQTT_PAYLOAD_LENGTH];
static app_mqtt_rx_message_t _app_mqtt_rx_message;

static hsys_timer_handle_t _periodic_timer;

void _timer_callback(void * arg);

void _app_mqtt_main_task(void * arg);
void _process_mqtt_messages(app_mqtt_rx_message_t * sensor_data);

static void _on_wifi_event(app_wifi_event_t event, void * arg);
static void _on_config_event(app_config_event_t event, void * arg);

int32_t app_mqtt_publish(const char * topic_type, uint8_t qos, const char * payload);

static pal_mqtt_config_t pal_mqtt_config = {0};

static fp_app_mqtt_on_event_t _on_event;
static hsys_mutex_handle_t _mqtt_publish_lock;
static fp_get_mqtt_sub_topic_t _get_mqtt_sub_topics_by_idx;

#define MAX_NO_SUB_TOPICS    (5)
static mqtt_topic_t _app_mqtt_sub_topics[MAX_NO_SUB_TOPICS];
static fp_on_mqtt_msg_t _app_mqtt_on_msg[MAX_NO_SUB_TOPICS];

static fp_wake_task_t _wake;
static void * _wake_context;

app_mqtt_init_t const * _app_mqtt_init;

static void _on_pal_mqtt_event(pal_mqtt_event_data_t* event){
    if(event == NULL) return;
    
    LOG_MSG_DEBUG(LOG_EN, "PAL MQTT Event: %d", (int)event->event_type);
    
    switch(event->event_type){
        case PAL_MQTT_EVENT_CONNECTED:
            if(_mqtt_events){ 
                LOG_MSG_DEBUG(LOG_EN, "MQTT Connected Event");        
                hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_MQTT_CONNECTED); 
            }            
            break;
            
        case PAL_MQTT_EVENT_DISCONNECTED:
            if(_mqtt_events){ 
                LOG_MSG_DEBUG(LOG_EN, "MQTT Disconnected Event");        
                hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_MQTT_DISCONNECTED); 
            } 
            break;
            
        case PAL_MQTT_EVENT_SUBSCRIBED:
            LOG_MSG_DEBUG(LOG_EN, "MQTT Subscribed Event");
            break;
            
        case PAL_MQTT_EVENT_UNSUBSCRIBED:
            LOG_MSG_DEBUG(LOG_EN, "MQTT Unsubscribed Event");
            break;
            
        case PAL_MQTT_EVENT_DATA:        
            if(_mqtt_events){ 
                // LOG_MSG_DEBUG(LOG_EN, "MQTT Message Received, Setting event");                  
                uint8_t is_locked = hsys_mutex_try_lock(_app_mqtt_rx_message_mutex, 2000);
                if(is_locked)
                {
                    pal_mqtt_message_t* msg = &event->data.message;
                    
                    _app_mqtt_rx_message.dup = msg->dup;
                    _app_mqtt_rx_message.qos = msg->qos;
                    _app_mqtt_rx_message.retain = msg->retain;
                    _app_mqtt_rx_message.len = msg->data_len;
                    _app_mqtt_rx_message.index = msg->current_data_offset;
                    _app_mqtt_rx_message.total = msg->total_data_len;
                    
                    // Copy topic (ensure null termination)
                    size_t topic_copy_len = msg->topic_len < (USER_CONFIG_MAX_MQTT_TOPIC_LENGTH - 1) ? 
                                           msg->topic_len : (USER_CONFIG_MAX_MQTT_TOPIC_LENGTH - 1);
                    memcpy(_app_mqtt_rx_message.topic, msg->topic, topic_copy_len);
                    _app_mqtt_rx_message.topic[topic_copy_len] = '\0';
                    
                    // Copy payload into dedicated static buffer (ensure null termination)
                    size_t payload_copy_len = msg->data_len < (USER_CONFIG_MAX_MQTT_PAYLOAD_LENGTH - 1) ? 
                                             msg->data_len : (USER_CONFIG_MAX_MQTT_PAYLOAD_LENGTH - 1);
                    memcpy(_mqtt_rx_payload_buf, msg->data, payload_copy_len);
                    _mqtt_rx_payload_buf[payload_copy_len] = '\0';
                    _app_mqtt_rx_message.payload = _mqtt_rx_payload_buf;
                    
                    hsys_mutex_unlock(_app_mqtt_rx_message_mutex);    
                    
                    LOG_MSG_DEBUG(LOG_EN, "MQTT Msg Received %ld", _app_mqtt_rx_message.len);
                    // LOG_DEBUG_BUFFER("RX2: ", (uint8_t *)_app_mqtt_rx_message.payload, _app_mqtt_rx_message.len);

                    hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_MQTT_MSG_RCVD);                    
                }
                else
                {
                    hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_MQTT_MSG_BUSY);                         
                }
            } 
            break;
            
        case PAL_MQTT_EVENT_PUBLISHED:
            LOG_MSG_DEBUG(LOG_EN, "MQTT Publish Sent, Setting event");
            // if(_mqtt_events){
            //     hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_PUBLISH_REQUESTED);
            // }
            break;
            
        case PAL_MQTT_EVENT_ERROR:
            LOG_MSG_ERROR(LOG_EN, "MQTT Error Event");
            break;
            
        default:
            break;
    }

    if(_on_event){
        // Map PAL event to app event
        _on_event((app_mqtt_event_t)event->event_type, event->user_data);
    }
    
    if(_wake){
        _wake(_wake_context);
    }
}

uint8_t _get_sub_topic(const char * topic){

    LOG_MSG_DEBUG(LOG_EN, "Getting sub topic for: %s", topic);
    for(int i = 0; i < MAX_NO_SUB_TOPICS; i++)
    {        
        if(strcmp(_app_mqtt_sub_topics[i].path, topic) == 0)
        {
            LOG_MSG_DEBUG(LOG_EN, "Found sub topic: %s", topic);
            return i;
        }
    }

    LOG_MSG_DEBUG(LOG_EN, "Sub topic not found: %s", topic);
    return 255;
}


uint8_t response_payalod[USER_CONFIG_MAX_MQTT_PAYLOAD_LENGTH];

void _process_mqtt_messages(app_mqtt_rx_message_t * mqtt_data){

    if(!_is_initialized)
    {
        LOG_MSG_ERROR(LOG_EN, "MQTT is not initialized");
        return;
    }

    if(mqtt_data == NULL)
    {
        LOG_MSG_ERROR(LOG_EN, "mqtt_data is NULL");
        return;
    }

    // if(mqtt_data->topic == NULL)
    // {
    //     LOG_MSG_ERROR(LOG_EN, "mqtt_data->topic is NULL");
    //     return;
    // }

    // LOG_MSG_DEBUG(LOG_EN, " ");
    // LOG_MSG_DEBUG(LOG_EN, "MQTT Msg Received");
    // LOG_MSG_DEBUG(LOG_EN, "RX: Topic: %s", mqtt_data->topic);
    // LOG_MSG_DEBUG(LOG_EN, "Payload: %s, %ld", mqtt_data->payload, mqtt_data->len);
    // LOG_DEBUG_BUFFER("Payload : ", mqtt_data->payload, (uint8_t)mqtt_data->len);
    // LOG_MSG_DEBUG(LOG_EN, " ");

    uint8_t sub_topic_idx = _get_sub_topic(mqtt_data->topic);
    if(sub_topic_idx == 255)
    {
        LOG_MSG_ERROR(LOG_EN, "Unknown sub topic: %s", mqtt_data->topic);
        return;
    }

    if(sub_topic_idx >= MAX_NO_SUB_TOPICS)
    {
        LOG_MSG_ERROR(LOG_EN, "Sub topic index out of range: %d", sub_topic_idx);
        return;
    }

    if(_app_mqtt_on_msg[sub_topic_idx] == NULL)
    {
        LOG_MSG_ERROR(LOG_EN, "No callback registered for sub topic: %s", mqtt_data->topic);
        return;
    }

    _app_mqtt_on_msg[sub_topic_idx](
        (const uint8_t *)mqtt_data->payload, mqtt_data->len);

    // app_mqtt_on_request(mqtt_data->payload, response_payalod, USER_CONFIG_MAX_MQTT_PAYLOAD_LENGTH);
    // if(ret == ERROR_OK)
    // {
    //     app_mqtt_publish("Response", 0, (const char *)response_payalod);
    // }

    // app_mqtt_sub_topic_t topic = _get_sub_topic(std::string(mqtt_data->topic));
    // LOG_MSG_DEBUG(LOG_EN, "Topic: %d , Type: %s", mqtt_data->topic, topic);

    // switch(topic){
    //     case APP_MQTT_TOPIC_REQUEST_THIS_DEVICE:{
            
    //     }
    //         break;
    //     case APP_MQTT_TOPIC_REQUEST_DEVICE_TYPE:
    //         break;
    //     case APP_MQTT_TOPIC_REQUEST_DEVICE_GROUP:
    //         break;
    //     case APP_MQTT_TOPIC_REQUEST_DEVICE_TYPE_DEVICE_GROUP:
    //         break;
    //     default:
    //         break;
    // }
}


static void _on_config_event(app_config_event_t event, void * arg){

    switch(event){
        case APP_CONFIG_EVENT_LOADED:
            hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_CONFIG_READY);
            LOG_MSG_DEBUG(LOG_EN, "MQTT Config Ready Event");
            break;
        default:
            break;
    }
    
    if(_wake){
        _wake(_wake_context);
    }
}


static void _on_internet_event(app_internet_event_t event, void * arg){

    switch(event){

        case APP_INTERNET_EVENT_CONNECTED:
            LOG_MSG_DEBUG(LOG_EN, "Internet Connected Event");
            hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_INTERNET_CONNECTED);
        break;
        case APP_INTERNET_EVENT_DISCONNECTED:
            LOG_MSG_DEBUG(LOG_EN, "Internet Disconnected Event");
            hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_INTERNET_DISCONNECTED);
        break;
        default:
        break;
    }
    
    if(_wake){
        _wake(_wake_context);
    }
}

static void _on_wifi_event(app_wifi_event_t event, void * arg){

    switch(event){
        case APP_WIFI_EVENT_STA_GOT_IP:
            hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_WIFI_CONNECTED);
            LOG_MSG_DEBUG(LOG_EN, "WiFI Connected Event");
            _is_wifi_connected = true;
            break;
        case APP_WIFI_EVENT_STA_DISCONNECTED:
            hsys_event_group_set_bits(_mqtt_events, MQTT_APP_EVENT_WIFI_DISCONNECTED);
            LOG_MSG_DEBUG(LOG_EN, "WiFI Disconnected Event");
            _is_wifi_connected = false;
            break;        
        default:
            break;
    }
    
    if(_wake){
        _wake(_wake_context);
    }
}

void app_mqtt_init(const app_mqtt_init_t * p_mqtt_init){
    
    if(p_mqtt_init->fp_app_mqtt_on_event == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. MQTT Null pointer reference, please check.., Critical Error");
        while (1);
    }
    _on_event = p_mqtt_init->fp_app_mqtt_on_event;

    if(p_mqtt_init->app_init.event_table == NULL){
        LOG_MSG_ERROR(LOG_EN, "Null pointer reference, please check.., Critical Error");
        while (1);        
    }

    if(NULL == p_mqtt_init->app_init.fp_wake || NULL == p_mqtt_init->app_init.wake_context){
        LOG_MSG_ERROR(LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    if(NULL == p_mqtt_init->fp_get_mqtt_sub_topic){
        LOG_MSG_ERROR(LOG_EN, "Critical Error! : fp_get_mqtt_sub_topic is NULL");
        while (1);
    }

    _get_mqtt_sub_topics_by_idx = p_mqtt_init->fp_get_mqtt_sub_topic;

    // _app_mqtt_request_table_size = 0;
    // if(NULL != p_mqtt_init->p_request_table){
    //     /* Null table also accepted */
    //     _p_app_mqtt_request_table = p_mqtt_init->p_request_table;
    //     _app_mqtt_request_table_size = p_mqtt_init->no_request_table_entries;
    // }
    
    _wake = p_mqtt_init->app_init.fp_wake;
    _wake_context = p_mqtt_init->app_init.wake_context;
    _app_mqtt_init = p_mqtt_init;
  
    _mqtt_events = hsys_event_group_create();
    _mqtt_publish_lock = hsys_mutex_create();
    _app_mqtt_rx_message_mutex = hsys_mutex_create();

    p_mqtt_init->app_init.event_table->on_wifi_event = (fp_event_interface_t)_on_wifi_event; 
    p_mqtt_init->app_init.event_table->on_config_event = (fp_event_interface_t)_on_config_event;  
    p_mqtt_init->app_init.event_table->on_internet_event = (fp_event_interface_t)_on_internet_event;

    _periodic_timer = hsys_timer_create("Periodic Timer", 30000, true, (void *)NULL, _timer_callback);
    hsys_start_timer(_periodic_timer);

    _is_initialized = true;
    LOG_MSG_DEBUG(LOG_EN, "MQTT initialized...");
    
    if(_wake){
        _wake(_wake_context);
    }
}

int32_t app_mqtt_publish(const char * topic, uint8_t qos, const char * payload)
{

    int32_t ret = ERROR_OK;
    do{

        if(topic == NULL){
            ret = ERROR_APP_INVALID_ARGUMENTS;
            break;
        }

        uint8_t is_locked = hsys_mutex_try_lock(_mqtt_publish_lock, 2000);
        if(is_locked) 
        {
            LOG_MSG_DEBUG(LOG_EN, "Publish to Topic %s", topic);
            LOG_MSG_DEBUG(LOG_EN, "Payload %s", payload);
            
            // Convert QoS
            pal_mqtt_qos_t pal_qos = (qos == 0) ? PAL_MQTT_QOS_0 : 
                                     (qos == 1) ? PAL_MQTT_QOS_1 : PAL_MQTT_QOS_2;
            
            int32_t msg_id = pal_mqtt_client_publish(_mqtt_client, topic, payload, 0, pal_qos, false);
            ret = (msg_id >= 0) ? ERROR_OK : ERROR_APP_BUSY;
            
            hsys_mutex_unlock(_mqtt_publish_lock);
        }
        else         
        {
            ret = ERROR_APP_BUSY;
        }
    }while(false);
    return ret;
}

enum app_mqtta_state_t{
    APP_MQTT_STATE_WAITING_FOR_WIFI,
    APP_MQTT_STATE_CONNECT,
    APP_MQTT_STATE_CONNECTING,
    APP_MQTT_STATE_SUBSCRIBE,
    APP_MQTT_STATE_RUNNING,
};


void app_mqtt_run()
{
    if(!_is_initialized){
        return;
    }

    LOG_MSG_DEBUG(LOG_EN, "MQTT Running...");

    static app_mqtta_state_t state = APP_MQTT_STATE_WAITING_FOR_WIFI;
    static unsigned long ts_mqtt_connect_reqsted = 0;
    static uint32_t wait_events;
    static uint32_t events;

    bool need_one_iteration = true;
    while(need_one_iteration){
        need_one_iteration = false;

        switch(state){

            case APP_MQTT_STATE_WAITING_FOR_WIFI:
            {
                wait_events = (MQTT_APP_EVENT_WIFI_CONNECTED | MQTT_APP_EVENT_INTERNET_CONNECTED | MQTT_APP_EVENT_CONFIG_READY);
                events = hsys_event_group_wait_bits(_mqtt_events, wait_events, 1, 1, 0);
                if((events & wait_events) == wait_events){                    
                    LOG_MSG_DEBUG(LOG_EN, "MQTT Starting...");

                    int32_t ret;
                    pal_mqtt_config_t hsys_mqtt_config_init;
                    ret = _app_mqtt_init->fp_app_mqtt_get_config(&hsys_mqtt_config_init, 2000);
                    if(ret == ERROR_OK){
                        // Convert legacy config to PAL config
                        pal_mqtt_get_default_config(&pal_mqtt_config);

                        // broker_uri already contains the full URI (e.g. "mqtt://144.24.156.245")
                        // built by app_mqtt_get_config — copy it directly.
                        memcpy(&pal_mqtt_config, &hsys_mqtt_config_init, sizeof(pal_mqtt_config_t));

                        // Set client ID (auto-generate if needed)
                        pal_mqtt_config.client_id[0] = '\0'; // Empty = auto-generate
                        
                        // Initialize PAL MQTT client
                        _mqtt_client = pal_mqtt_client_init(&pal_mqtt_config, _on_pal_mqtt_event, NULL);
                        if(_mqtt_client != NULL) {
                            state = APP_MQTT_STATE_CONNECT;
                            need_one_iteration = true;
                        } else {
                            LOG_MSG_ERROR(LOG_EN, "Failed to initialize MQTT client");
                        }
                    }
                }
            }
            break;

            case APP_MQTT_STATE_CONNECT:        
            {
                LOG_MSG_DEBUG(LOG_EN, "MQTT Connecting...");
                ts_mqtt_connect_reqsted = board_millis();
                pal_mqtt_client_start(_mqtt_client);
                state = APP_MQTT_STATE_CONNECTING;
                need_one_iteration = true;                
            }
            break;

            case APP_MQTT_STATE_CONNECTING:
            {
                wait_events = (MQTT_APP_EVENT_MQTT_CONNECTED | MQTT_APP_EVENT_WIFI_DISCONNECTED | MQTT_APP_EVENT_MQTT_DISCONNECTED);
                events = hsys_event_group_wait_bits(_mqtt_events, wait_events, 1, 0, 0);

                if(events & MQTT_APP_EVENT_WIFI_DISCONNECTED){
                    LOG_MSG_DEBUG(LOG_EN, "WiFi Disconnected");
                    state = APP_MQTT_STATE_WAITING_FOR_WIFI;
                }
                else if(events & MQTT_APP_EVENT_MQTT_DISCONNECTED){
                    LOG_MSG_DEBUG(LOG_EN, "MQTT Disconnected");
                    state = APP_MQTT_STATE_CONNECT;
                }
                else if(events & MQTT_APP_EVENT_MQTT_CONNECTED){
                    LOG_MSG_DEBUG(LOG_EN, "MQTT Connected");
                    state = APP_MQTT_STATE_SUBSCRIBE;
                    need_one_iteration = true;
                }           
            }
            break;

            case APP_MQTT_STATE_SUBSCRIBE:
            {            
                pal_mqtt_client_publish(_mqtt_client, "device/ABCDEF123456/", "Testing...", 0, PAL_MQTT_QOS_0, false);

                for(uint8_t i=0; i<MAX_NO_SUB_TOPICS; i++)
                {
                    const char * topic = _get_mqtt_sub_topics_by_idx(i, _app_mqtt_sub_topics[i].path, &_app_mqtt_on_msg[i]);
                    if(topic == NULL)
                    {
                        LOG_MSG_ERROR(LOG_EN, "Failed to get MQTT sub topic for index %d", i);
                        break;
                    }

                    LOG_MSG_DEBUG(LOG_EN, "Subscribing to %s", topic);
                    pal_mqtt_client_subscribe(_mqtt_client, topic, PAL_MQTT_QOS_0);
                }

                // sprintf(temp_mqtt_topic.path, "%s/v1/dev/command/%s", hsys_mqtt_config_init.base_topic,  hsys_mqtt_config_init.device_uuid);
                // LOG_MSG_DEBUG(LOG_EN, "Subscribing to %s", temp_mqtt_topic.path);
                // hsys_mqtt_subscribe(temp_mqtt_topic.path, 0);

                // sprintf(temp_mqtt_topic.path, "%s/v1/dev/request/%s", hsys_mqtt_config_init.base_topic,  hsys_mqtt_config_init.device_uuid);
                // LOG_MSG_DEBUG(LOG_EN, "Subscribing to %s", temp_mqtt_topic.path);
                // hsys_mqtt_subscribe(temp_mqtt_topic.path, 0);

                // sprintf(temp_mqtt_topic.path, "%s/v1/dev/request/%s", hsys_mqtt_config_init.base_topic,  hsys_mqtt_config_init.device_type);
                // LOG_MSG_DEBUG(LOG_EN, "Subscribing to %s", temp_mqtt_topic.path);
                // hsys_mqtt_subscribe(temp_mqtt_topic.path, 0);

                // sprintf(temp_mqtt_topic.path, "%s/v1/dev/request/%s", hsys_mqtt_config_init.base_topic,  hsys_mqtt_config_init.device_group);
                // LOG_MSG_DEBUG(LOG_EN, "Subscribing to %s", temp_mqtt_topic.path);
                // hsys_mqtt_subscribe(temp_mqtt_topic.path, 0);

                // sprintf(temp_mqtt_topic.path, "%s/v1/dev/request/%s/%s", hsys_mqtt_config_init.base_topic,  hsys_mqtt_config_init.device_group, hsys_mqtt_config_init.device_type);
                // LOG_MSG_DEBUG(LOG_EN, "Subscribing to %s", temp_mqtt_topic.path);
                // hsys_mqtt_subscribe(temp_mqtt_topic.path, 0);

                state = APP_MQTT_STATE_RUNNING; 
                need_one_iteration = true;           
            }
            break;

            case APP_MQTT_STATE_RUNNING:
            {
                wait_events = (MQTT_APP_EVENT_WIFI_DISCONNECTED | MQTT_APP_EVENT_MQTT_DISCONNECTED | MQTT_APP_EVENT_MQTT_MSG_RCVD);
                events = hsys_event_group_wait_bits(_mqtt_events, wait_events, 1, 0, 0);
                
                if((events & MQTT_APP_EVENT_WIFI_DISCONNECTED) || (!_is_wifi_connected)){
                    LOG_MSG_DEBUG(LOG_EN, "MQTT Disconnected, because WiFi or Mqtt Disconnected");
                    state = APP_MQTT_STATE_WAITING_FOR_WIFI;
                    need_one_iteration = true;
                }

                if(events & MQTT_APP_EVENT_MQTT_DISCONNECTED){
                    state = APP_MQTT_STATE_CONNECT;
                    need_one_iteration = true;
                }

                if(events & MQTT_APP_EVENT_MQTT_MSG_RCVD){
                    // LOG_MSG_DEBUG(LOG_EN, "MQTT Message Received");
                    _process_mqtt_messages(&_app_mqtt_rx_message);
                } 
                
                if(!pal_mqtt_client_is_connected(&_mqtt_client)){
                    state = APP_MQTT_STATE_CONNECT;
                    need_one_iteration = true;
                }
            }
            break;
            
            default:
                break;
        }
    }
}



// void _app_mqtt_main_task(void * arg){

//     hsys_event_group_wait_bits(_mqtt_events, 
//         (MQTT_APP_EVENT_CONFIG_READY | MQTT_APP_EVENT_WIFI_CONNECTED | MQTT_APP_EVENT_INTERNET_CONNECTED), 
//         pdTRUE, pdTRUE, portMAX_DELAY);

//     app_config_get_mqtt_init(&mqtt_config_init, portMAX_DELAY);
//     app_config_get_app_settings(&app_settings_init, portMAX_DELAY);
//     // app_config_get_device(&app_device, portMAX_DELAY);

//     _device_group = std::string(app_settings_init.device_group);
//     // _device_type = std::string(app_device.device_type.data);
//     // _device_mac = std::string(app_device.mac.data);

//     // std::string board_id; 
//     // uint8_t mac[SIZE_OF_MAC];
//     // board_get_board_id(&board_id, mac, SIZE_OF_MAC);
//     // _mqtt_topic_publish_device = std::string(mqtt_config_init.publish) + "/" + board_id;
//     // _mqtt_topic_publish_project = std::string(mqtt_config_init.publish);
//     // _mqtt_topic_subscribe_project = std::string(mqtt_config_init.subscribe);   
//     // _mqtt_topic_subscribe_device = std::string(mqtt_config_init.subscribe)  + "/" + board_id;

//     // _topics_subscribed[APP_MQTT_TOPIC_REQUEST_THIS_DEVICE] = std::string(mqtt_config_init.subscribe) + "/v1/dev/request/" + std::string(app_device.board_uuid.data);

//     // _topics_published[APP_MQTT_TOPIC_PUBLISH_EVENT_THIS_DEVICE] = std::string(mqtt_config_init.publish) + "/v1/svr/event/" + _device_type +"/"+ _device_group + "/" + std::string(app_device.board_uuid.data);
//     // _topics_published[APP_MQTT_TOPIC_PUBLISH_STARTUP_THIS_DEVICE] = std::string(mqtt_config_init.publish) + "/v1/svr/startup/" + _device_type +"/"+ _device_group + "/" + std::string(app_device.board_uuid.data);
//     // _topics_published[APP_MQTT_TOPIC_PUBLISH_RECON_THIS_DEVICE] = std::string(mqtt_config_init.publish) + "/v1/svr/recon/" + _device_type +"/"+ _device_group + "/" + std::string(app_device.board_uuid.data);
//     // _topics_published[APP_MQTT_TOPIC_PUBLISH_RESPONSE_THIS_DEVICE] = std::string(mqtt_config_init.publish) + "/v1/svr/response/" + _device_type +"/"+ _device_group + "/" + std::string(app_device.board_uuid.data);

//     memccpy(hsys_mqtt_config_init.host, mqtt_config_init.host, 0, SIZE_OF_MQTT_HOST);
//     hsys_mqtt_config_init.port = mqtt_config_init.port;

//     hsys_mqtt_init(hsys_mqtt_config_init, &_mqtt, _on_hsys_mqtt_event);
//     hsys_mqtt_connect(&_mqtt);

//     uint32_t app_event_bits;
//     while(1){
        
//         uint32_t events_waiting = (
//             MQTT_APP_EVENT_MQTT_CONNECTED | 
//             MQTT_APP_EVENT_WIFI_CONNECTED |
//             MQTT_APP_EVENT_MQTT_MSG_RCVD |
//             MQTT_APP_EVENT_MQTT_RECONNECT_REQUIRED |
//             MQTT_APP_EVENT_INTERNET_CONNECTED
//         );
//         uint32_t app_event_bits = hsys_event_group_wait_bits(_mqtt_events, events_waiting, pdTRUE, pdFALSE, portMAX_DELAY);
        
//         if(app_event_bits & MQTT_APP_EVENT_WIFI_CONNECTED){ 
//             hsys_event_group_clear_bits(_mqtt_events, MQTT_APP_EVENT_WIFI_CONNECTED);
//             LOG_MSG_DEBUG(LOG_EN, "Wifi Reconnected, So reconnecting to MQTT (NA)");          
//             hsys_mqtt_connect(&_mqtt);
//         }        

//         if(
//             (app_event_bits &  MQTT_APP_EVENT_INTERNET_CONNECTED) ||
//             (app_event_bits &  MQTT_APP_EVENT_MQTT_RECONNECT_REQUIRED) )
//         { 
//             hsys_event_group_clear_bits(_mqtt_events, MQTT_APP_EVENT_INTERNET_CONNECTED);  
//             hsys_event_group_clear_bits(_mqtt_events, MQTT_APP_EVENT_MQTT_RECONNECT_REQUIRED);          
//             LOG_MSG_DEBUG(LOG_EN, "MQTT is not connected, Retry Connecting... ");
//             hsys_mqtt_disconnect(&_mqtt);
//             hsys_mqtt_connect(&_mqtt);
//         }

//         if((app_event_bits & MQTT_APP_EVENT_MQTT_CONNECTED)){ 
//             hsys_event_group_clear_bits(_mqtt_events, MQTT_APP_EVENT_MQTT_CONNECTED);
//             hsys_mqtt_subscribe(_topics_subscribed[APP_MQTT_TOPIC_REQUEST_THIS_DEVICE], 1);
//         }

//         if(app_event_bits & MQTT_APP_EVENT_MQTT_MSG_RCVD){            
//             _process_mqtt_messages(&_app_mqtt_rx_message); 
//         }
//     }

//     hsys_task_delete(_app_mqtt_main_task_handle);
//     hsys_event_group_delete(_mqtt_events);
//     hsys_queue_deinit(&_mqtt_appsensor_data_queue);
// }


void _timer_callback(void * arg)
{    
    if(_wake){
        // LOG_MSG_DEBUG(LOG_EN, "Wake From Timer");
        _wake(_wake_context);
    }
}