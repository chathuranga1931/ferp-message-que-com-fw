/**
 * @file pal_mqtt.h
 * @brief Platform Abstraction Layer for MQTT operations
 * 
 * This header provides a platform-independent interface for MQTT client
 * functionality including connection management, publish/subscribe operations,
 * and event handling.
 */

#ifndef PAL_MQTT_H
#define PAL_MQTT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

#define PAL_MQTT_MAX_BROKER_URI_LEN     (512+128)
#define PAL_MQTT_MAX_CLIENT_ID_LEN      64
#define PAL_MQTT_MAX_USERNAME_LEN       64
#define PAL_MQTT_MAX_PASSWORD_LEN       64
#define PAL_MQTT_MAX_TOPIC_LEN          256
#define PAL_MQTT_MAX_LWT_TOPIC_LEN      128
#define PAL_MQTT_MAX_LWT_MSG_LEN        128

// ============================================================================
// Type Definitions
// ============================================================================

/**
 * @brief MQTT Quality of Service levels
 */
typedef enum {
    PAL_MQTT_QOS_0 = 0,     /**< At most once delivery */
    PAL_MQTT_QOS_1 = 1,     /**< At least once delivery */
    PAL_MQTT_QOS_2 = 2,     /**< Exactly once delivery */
} pal_mqtt_qos_t;

/**
 * @brief MQTT transport types
 */
typedef enum {
    PAL_MQTT_TRANSPORT_UNKNOWN = 0,
    PAL_MQTT_TRANSPORT_OVER_TCP,      /**< MQTT over TCP */
    PAL_MQTT_TRANSPORT_OVER_SSL,      /**< MQTT over SSL/TLS */
    PAL_MQTT_TRANSPORT_OVER_WS,       /**< MQTT over WebSocket */
    PAL_MQTT_TRANSPORT_OVER_WSS,      /**< MQTT over WebSocket Secure */
} pal_mqtt_transport_t;

/**
 * @brief MQTT event types
 */
typedef enum {
    PAL_MQTT_EVENT_CONNECTED,         /**< Connected to broker */
    PAL_MQTT_EVENT_DISCONNECTED,      /**< Disconnected from broker */
    PAL_MQTT_EVENT_SUBSCRIBED,        /**< Subscribed to topic */
    PAL_MQTT_EVENT_UNSUBSCRIBED,      /**< Unsubscribed from topic */
    PAL_MQTT_EVENT_PUBLISHED,         /**< Message published successfully */
    PAL_MQTT_EVENT_DATA,              /**< Data received from subscribed topic */
    PAL_MQTT_EVENT_ERROR,             /**< Error occurred */
} pal_mqtt_event_t;

/**
 * @brief MQTT client handle (opaque pointer)
 */
typedef void* pal_mqtt_client_handle_t;

/**
 * @brief MQTT Last Will and Testament (LWT) configuration
 */
typedef struct {
    char topic[PAL_MQTT_MAX_LWT_TOPIC_LEN];     /**< LWT topic */
    char message[PAL_MQTT_MAX_LWT_MSG_LEN];     /**< LWT message */
    pal_mqtt_qos_t qos;                          /**< LWT QoS */
    bool retain;                                 /**< LWT retain flag */
} pal_mqtt_lwt_config_t;

/**
 * @brief MQTT client configuration
 */
typedef struct {
    // Broker settings
    char broker_uri[PAL_MQTT_MAX_BROKER_URI_LEN];   /**< Broker URI (mqtt://host:port or mqtts://host:port) */
    uint16_t port;                                   /**< Broker port (0 = use default) */
    pal_mqtt_transport_t transport;                  /**< Transport type */
    
    // Client identification
    char client_id[PAL_MQTT_MAX_CLIENT_ID_LEN];    /**< Client ID (empty = auto-generate) */
    char username[PAL_MQTT_MAX_USERNAME_LEN];      /**< Username (optional) */
    char password[PAL_MQTT_MAX_PASSWORD_LEN];      /**< Password (optional) */
    
    // Connection settings
    uint16_t keepalive;                             /**< Keep-alive interval in seconds (0 = default 120) */
    bool disable_clean_session;                     /**< Disable clean session flag */
    
    // Last Will and Testament
    bool use_lwt;                                   /**< Enable LWT */
    pal_mqtt_lwt_config_t lwt;                      /**< LWT configuration */
    
    // Network settings
    uint32_t network_timeout_ms;                    /**< Network timeout in milliseconds */
    uint32_t reconnect_timeout_ms;                  /**< Reconnect timeout in milliseconds */
    
    // Buffer sizes
    size_t buffer_size;                             /**< Buffer size for TX/RX (0 = default) */
    
    // TLS/SSL settings (when using secure transport)
    const char* cert_pem;                           /**< Server certificate (NULL = skip verification) */
    const char* client_cert_pem;                    /**< Client certificate (for mutual auth) */
    const char* client_key_pem;                     /**< Client private key (for mutual auth) */
    bool skip_cert_common_name_check;               /**< Skip CN check in cert verification */
} pal_mqtt_config_t;

/**
 * @brief MQTT received message data
 */
typedef struct {
    const char* topic;          /**< Topic of received message */
    size_t topic_len;           /**< Length of topic */
    const char* data;           /**< Message payload data */
    size_t data_len;            /**< Length of data */
    size_t current_data_offset; /**< Offset for chunked messages */
    size_t total_data_len;      /**< Total length for chunked messages */
    pal_mqtt_qos_t qos;         /**< QoS level */
    bool retain;                /**< Retain flag */
    bool dup;                   /**< Duplicate flag */
} pal_mqtt_message_t;

/**
 * @brief MQTT event data
 */
typedef struct {
    pal_mqtt_event_t event_type;        /**< Type of event */
    pal_mqtt_client_handle_t client;    /**< Client handle */
    union {
        pal_mqtt_message_t message;     /**< Message data (for DATA event) */
        int32_t error_code;              /**< Error code (for ERROR event) */
        struct {
            int msg_id;                  /**< Message ID */
        } published;                     /**< Published event data */
        struct {
            int msg_id;                  /**< Message ID */
        } subscribed;                    /**< Subscribed event data */
    } data;
    void* user_data;                     /**< User data pointer */
} pal_mqtt_event_data_t;

/**
 * @brief MQTT event callback function type
 * 
 * @param event Event data
 */
typedef void (*pal_mqtt_event_callback_t)(pal_mqtt_event_data_t* event);

// ============================================================================
// MQTT Client Functions
// ============================================================================

/**
 * @brief Initialize MQTT client with configuration
 * 
 * @param config Pointer to MQTT client configuration
 * @param event_callback Event callback function
 * @param user_data User data pointer to pass to callback
 * @return MQTT client handle on success, NULL on failure
 */
pal_mqtt_client_handle_t pal_mqtt_client_init(const pal_mqtt_config_t* config,
                                               pal_mqtt_event_callback_t event_callback,
                                               void* user_data);

/**
 * @brief Start MQTT client (connect to broker)
 * 
 * @param client MQTT client handle
 * @return 0 on success, negative error code on failure
 */
int32_t pal_mqtt_client_start(pal_mqtt_client_handle_t client);

/**
 * @brief Stop MQTT client (disconnect from broker)
 * 
 * @param client MQTT client handle
 * @return 0 on success, negative error code on failure
 */
int32_t pal_mqtt_client_stop(pal_mqtt_client_handle_t client);

/**
 * @brief Destroy MQTT client and free resources
 * 
 * @param client MQTT client handle
 * @return 0 on success, negative error code on failure
 */
int32_t pal_mqtt_client_destroy(pal_mqtt_client_handle_t client);

/**
 * @brief Reconnect to MQTT broker
 * 
 * @param client MQTT client handle
 * @return 0 on success, negative error code on failure
 */
int32_t pal_mqtt_client_reconnect(pal_mqtt_client_handle_t client);

// ============================================================================
// MQTT Publish/Subscribe Functions
// ============================================================================

/**
 * @brief Subscribe to MQTT topic
 * 
 * @param client MQTT client handle
 * @param topic Topic to subscribe to
 * @param qos QoS level for subscription
 * @return Message ID on success, negative error code on failure
 */
int32_t pal_mqtt_client_subscribe(pal_mqtt_client_handle_t client,
                                   const char* topic,
                                   pal_mqtt_qos_t qos);

/**
 * @brief Unsubscribe from MQTT topic
 * 
 * @param client MQTT client handle
 * @param topic Topic to unsubscribe from
 * @return Message ID on success, negative error code on failure
 */
int32_t pal_mqtt_client_unsubscribe(pal_mqtt_client_handle_t client,
                                     const char* topic);

/**
 * @brief Publish message to MQTT topic
 * 
 * @param client MQTT client handle
 * @param topic Topic to publish to
 * @param data Message payload data
 * @param len Length of data (0 = strlen(data))
 * @param qos QoS level
 * @param retain Retain flag
 * @return Message ID on success, negative error code on failure
 */
int32_t pal_mqtt_client_publish(pal_mqtt_client_handle_t client,
                                 const char* topic,
                                 const char* data,
                                 size_t len,
                                 pal_mqtt_qos_t qos,
                                 bool retain);

// ============================================================================
// MQTT Status Functions
// ============================================================================

/**
 * @brief Check if MQTT client is connected to broker
 * 
 * @param client MQTT client handle
 * @return true if connected, false otherwise
 */
bool pal_mqtt_client_is_connected(pal_mqtt_client_handle_t client);

/**
 * @brief Get MQTT client connection state
 * 
 * @param client MQTT client handle
 * @return 1 if connected, 0 if disconnected, negative on error
 */
int32_t pal_mqtt_client_get_state(pal_mqtt_client_handle_t client);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get default MQTT configuration
 * 
 * @param config Pointer to configuration structure to fill with defaults
 */
void pal_mqtt_get_default_config(pal_mqtt_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // PAL_MQTT_H
