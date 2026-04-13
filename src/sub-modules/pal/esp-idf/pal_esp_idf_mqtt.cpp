/**
 * @file pal_esp_idf_mqtt.cpp
 * @brief ESP-IDF implementation of MQTT PAL
 * 
 * This implementation uses ESP-IDF's esp-mqtt component (mqtt_client)
 * to provide MQTT functionality for ESP32 chips.
 */

#include "pal_mqtt.h"

#include <string.h>
#include <stdio.h>

#include "mqtt_client.h"
#include "esp_log.h"

static const char* TAG = "PAL_MQTT";

// ============================================================================
// Internal Client Structure
// ============================================================================

typedef struct {
    esp_mqtt_client_handle_t esp_client;
    pal_mqtt_event_callback_t event_callback;
    void* user_data;
    bool is_connected;
} pal_mqtt_client_internal_t;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Convert PAL QoS to ESP-IDF QoS
 */
static int pal_qos_to_esp_qos(pal_mqtt_qos_t qos) {
    switch (qos) {
        case PAL_MQTT_QOS_0: return 0;
        case PAL_MQTT_QOS_1: return 1;
        case PAL_MQTT_QOS_2: return 2;
        default: return 0;
    }
}

/**
 * @brief Convert ESP-IDF QoS to PAL QoS
 */
static pal_mqtt_qos_t esp_qos_to_pal_qos(int qos) {
    switch (qos) {
        case 0: return PAL_MQTT_QOS_0;
        case 1: return PAL_MQTT_QOS_1;
        case 2: return PAL_MQTT_QOS_2;
        default: return PAL_MQTT_QOS_0;
    }
}

// ============================================================================
// Event Handler
// ============================================================================

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                               int32_t event_id, void* event_data) {
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    if (internal_client == NULL || internal_client->event_callback == NULL) {
        return;
    }
    
    pal_mqtt_event_data_t pal_event = {};
    pal_event.event_type = PAL_MQTT_EVENT_ERROR;
    pal_event.client = (pal_mqtt_client_handle_t)internal_client;
    pal_event.user_data = internal_client->user_data;
    pal_event.data = {0};
    
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            internal_client->is_connected = true;
            pal_event.event_type = PAL_MQTT_EVENT_CONNECTED;
            internal_client->event_callback(&pal_event);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            internal_client->is_connected = false;
            pal_event.event_type = PAL_MQTT_EVENT_DISCONNECTED;
            internal_client->event_callback(&pal_event);
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            pal_event.event_type = PAL_MQTT_EVENT_SUBSCRIBED;
            pal_event.data.subscribed.msg_id = event->msg_id;
            internal_client->event_callback(&pal_event);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            pal_event.event_type = PAL_MQTT_EVENT_UNSUBSCRIBED;
            internal_client->event_callback(&pal_event);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            pal_event.event_type = PAL_MQTT_EVENT_PUBLISHED;
            pal_event.data.published.msg_id = event->msg_id;
            internal_client->event_callback(&pal_event);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            pal_event.event_type = PAL_MQTT_EVENT_DATA;
            
            // Fill message data
            pal_event.data.message.topic = event->topic;
            pal_event.data.message.topic_len = event->topic_len;
            pal_event.data.message.data = event->data;
            pal_event.data.message.data_len = event->data_len;
            pal_event.data.message.current_data_offset = event->current_data_offset;
            pal_event.data.message.total_data_len = event->total_data_len;
            pal_event.data.message.qos = esp_qos_to_pal_qos(event->qos);
            pal_event.data.message.retain = event->retain;
            pal_event.data.message.dup = event->dup;
            
            internal_client->event_callback(&pal_event);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            pal_event.event_type = PAL_MQTT_EVENT_ERROR;
            pal_event.data.error_code = -1;
            internal_client->event_callback(&pal_event);
            break;
            
        default:
            ESP_LOGD(TAG, "MQTT event: %d", event_id);
            break;
    }
}

// ============================================================================
// MQTT Client Functions
// ============================================================================

void pal_mqtt_get_default_config(pal_mqtt_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(pal_mqtt_config_t));
    config->keepalive = 120;
    config->network_timeout_ms = 10000;
    config->reconnect_timeout_ms = 10000;
    config->buffer_size = 1024;
    config->transport = PAL_MQTT_TRANSPORT_OVER_TCP;
}

pal_mqtt_client_handle_t pal_mqtt_client_init(const pal_mqtt_config_t* config,
                                               pal_mqtt_event_callback_t event_callback,
                                               void* user_data) {
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    // Allocate internal client structure
    pal_mqtt_client_internal_t* internal_client = 
        (pal_mqtt_client_internal_t*)calloc(1, sizeof(pal_mqtt_client_internal_t));
    
    if (internal_client == NULL) {
        ESP_LOGE(TAG, "Failed to allocate client structure");
        return NULL;
    }
    
    internal_client->event_callback = event_callback;
    internal_client->user_data = user_data;
    internal_client->is_connected = false;
    
    // Configure ESP-IDF MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {0};
    
    // Broker URI
    mqtt_cfg.broker.address.uri = config->broker_uri;
    if (config->port > 0) {
        mqtt_cfg.broker.address.port = config->port;
    }
    
    // Client credentials
    if (strlen(config->client_id) > 0) {
        mqtt_cfg.credentials.client_id = config->client_id;
    }
    if (strlen(config->username) > 0) {
        mqtt_cfg.credentials.username = config->username;
    }
    if (strlen(config->password) > 0) {
        mqtt_cfg.credentials.authentication.password = config->password;
    }
    
    // Session settings
    mqtt_cfg.session.keepalive = config->keepalive > 0 ? config->keepalive : 120;
    mqtt_cfg.session.disable_clean_session = config->disable_clean_session;
    
    // Last Will and Testament
    if (config->use_lwt) {
        mqtt_cfg.session.last_will.topic = config->lwt.topic;
        mqtt_cfg.session.last_will.msg = config->lwt.message;
        mqtt_cfg.session.last_will.msg_len = strlen(config->lwt.message);
        mqtt_cfg.session.last_will.qos = pal_qos_to_esp_qos(config->lwt.qos);
        mqtt_cfg.session.last_will.retain = config->lwt.retain;
    }
    
    // Network settings
    mqtt_cfg.network.timeout_ms = config->network_timeout_ms > 0 ? 
                                  config->network_timeout_ms : 10000;
    mqtt_cfg.network.reconnect_timeout_ms = config->reconnect_timeout_ms > 0 ? 
                                            config->reconnect_timeout_ms : 10000;
    
    // Buffer size
    if (config->buffer_size > 0) {
        mqtt_cfg.buffer.size = config->buffer_size;
        mqtt_cfg.buffer.out_size = config->buffer_size;
    }
    
    // TLS/SSL settings
    if (config->cert_pem != NULL) {
        mqtt_cfg.broker.verification.certificate = config->cert_pem;
    }
    if (config->client_cert_pem != NULL) {
        mqtt_cfg.credentials.authentication.certificate = config->client_cert_pem;
    }
    if (config->client_key_pem != NULL) {
        mqtt_cfg.credentials.authentication.key = config->client_key_pem;
    }
    if (config->skip_cert_common_name_check) {
        mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
    }
    
    // Initialize ESP-IDF MQTT client
    internal_client->esp_client = esp_mqtt_client_init(&mqtt_cfg);
    
    if (internal_client->esp_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        free(internal_client);
        return NULL;
    }
    
    // Register event handler
    esp_mqtt_client_register_event(internal_client->esp_client,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   internal_client);
    
    ESP_LOGI(TAG, "MQTT client initialized");
    
    return (pal_mqtt_client_handle_t)internal_client;
}

int32_t pal_mqtt_client_start(pal_mqtt_client_handle_t client) {
    if (client == NULL) {
        ESP_LOGE(TAG, "Client is NULL");
        return -1;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    
    esp_err_t ret = esp_mqtt_client_start(internal_client->esp_client);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %d", ret);
        return -1;
    }
    
    ESP_LOGI(TAG, "MQTT client started");
    return 0;
}

int32_t pal_mqtt_client_stop(pal_mqtt_client_handle_t client) {
    if (client == NULL) {
        return 0;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    
    esp_err_t ret = esp_mqtt_client_stop(internal_client->esp_client);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client: %d", ret);
        return -1;
    }
    
    internal_client->is_connected = false;
    
    ESP_LOGI(TAG, "MQTT client stopped");
    return 0;
}

int32_t pal_mqtt_client_destroy(pal_mqtt_client_handle_t client) {
    if (client == NULL) {
        return 0;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    
    // Stop client first
    pal_mqtt_client_stop(client);
    
    // Destroy ESP-IDF client
    esp_mqtt_client_destroy(internal_client->esp_client);
    
    // Free internal structure
    free(internal_client);
    
    ESP_LOGI(TAG, "MQTT client destroyed");
    return 0;
}

int32_t pal_mqtt_client_reconnect(pal_mqtt_client_handle_t client) {
    if (client == NULL) {
        ESP_LOGE(TAG, "Client is NULL");
        return -1;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    
    esp_err_t ret = esp_mqtt_client_reconnect(internal_client->esp_client);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconnect MQTT client: %d", ret);
        return -1;
    }
    
    ESP_LOGI(TAG, "MQTT client reconnecting");
    return 0;
}

// ============================================================================
// MQTT Publish/Subscribe Functions
// ============================================================================

int32_t pal_mqtt_client_subscribe(pal_mqtt_client_handle_t client,
                                   const char* topic,
                                   pal_mqtt_qos_t qos) {
    if (client == NULL || topic == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    
    int msg_id = esp_mqtt_client_subscribe(internal_client->esp_client,
                                          topic,
                                          pal_qos_to_esp_qos(qos));
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
        return -1;
    }
    
    ESP_LOGI(TAG, "Subscribed to topic: %s, msg_id=%d", topic, msg_id);
    return msg_id;
}

int32_t pal_mqtt_client_unsubscribe(pal_mqtt_client_handle_t client,
                                     const char* topic) {
    if (client == NULL || topic == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    
    int msg_id = esp_mqtt_client_unsubscribe(internal_client->esp_client, topic);
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to unsubscribe from topic: %s", topic);
        return -1;
    }
    
    ESP_LOGI(TAG, "Unsubscribed from topic: %s, msg_id=%d", topic, msg_id);
    return msg_id;
}

int32_t pal_mqtt_client_publish(pal_mqtt_client_handle_t client,
                                 const char* topic,
                                 const char* data,
                                 size_t len,
                                 pal_mqtt_qos_t qos,
                                 bool retain) {
    if (client == NULL || topic == NULL || data == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    
    // If len is 0, calculate from string
    int data_len = (len == 0) ? strlen(data) : len;
    
    int msg_id = esp_mqtt_client_publish(internal_client->esp_client,
                                        topic,
                                        data,
                                        data_len,
                                        pal_qos_to_esp_qos(qos),
                                        retain ? 1 : 0);
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to topic: %s", topic);
        return -1;
    }
    
    ESP_LOGI(TAG, "Published to topic: %s, msg_id=%d", topic, msg_id);
    return msg_id;
}

// ============================================================================
// MQTT Status Functions
// ============================================================================

bool pal_mqtt_client_is_connected(pal_mqtt_client_handle_t client) {
    if (client == NULL) {
        return false;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    return internal_client->is_connected;
}

int32_t pal_mqtt_client_get_state(pal_mqtt_client_handle_t client) {
    if (client == NULL) {
        return -1;
    }
    
    pal_mqtt_client_internal_t* internal_client = (pal_mqtt_client_internal_t*)client;
    return internal_client->is_connected ? 1 : 0;
}
