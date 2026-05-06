/**
 * @file pal_esp_idf_http_client.cpp
 * @brief ESP-IDF implementation of HTTP client PAL
 */

#include "pal_http_client.h"
#include "pal_logger.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "esp_system.h"
#include <string.h>
#include <stdlib.h>

#define __TAG__ "PAL_HTTP"

#define HTTP_ERROR_LOG_EN      LOG_DIS

// Internal structure for HTTP client
typedef struct {
    esp_http_client_handle_t esp_client;
    char* response_buffer;
    size_t response_len;
    size_t response_capacity;
    char* header_keys[PAL_HTTP_MAX_HEADERS];      // Keys we want to collect
    char* header_values[PAL_HTTP_MAX_HEADERS];    // Captured header values
    uint8_t header_count;                          // Number of headers to collect
    // Streaming support
    pal_http_stream_chunk_cb_t stream_chunk_cb;
    void*                      stream_user_ctx;
    bool                       stream_abort;
} pal_http_client_internal_t;

// Event handler for HTTP client
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    pal_http_client_internal_t* client = (pal_http_client_internal_t*)evt->user_data;
    
    switch(evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            LOG_MSG_INFO(HTTP_ERROR_LOG_EN, "Header received: %s: %s", evt->header_key, evt->header_value);
            // Capture headers as they arrive
            for (uint8_t i = 0; i < client->header_count; i++) {
                if (client->header_keys[i] != NULL && 
                    strcasecmp(evt->header_key, client->header_keys[i]) == 0) {
                    // Found a header we're interested in - store its value
                    LOG_MSG_INFO(HTTP_ERROR_LOG_EN, "Capturing header: %s = %s", evt->header_key, evt->header_value);
                    if (client->header_values[i] != NULL) {
                        free(client->header_values[i]);
                    }
                    client->header_values[i] = strdup(evt->header_value);
                    break;
                }
            }
            break;
            
        case HTTP_EVENT_ON_DATA:
            if (client->stream_chunk_cb != NULL) {
                // Streaming mode: deliver chunk directly to callback
                if (!client->stream_abort) {
                    int32_t ret = client->stream_chunk_cb(
                        (const uint8_t*)evt->data, (size_t)evt->data_len,
                        client->stream_user_ctx);
                    if (ret != 0) {
                        client->stream_abort = true;
                    }
                }
            } else {
                // Buffered mode: accumulate into response_buffer
                // Resize buffer if needed
                if (client->response_len + evt->data_len >= client->response_capacity) {
                    size_t new_capacity = client->response_capacity * 2 + evt->data_len;
                    char* new_buffer = (char*)realloc(client->response_buffer, new_capacity);
                    if (new_buffer == NULL) {
                        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to allocate response buffer");
                        return ESP_FAIL;
                    }
                    client->response_buffer = new_buffer;
                    client->response_capacity = new_capacity;
                }
                // Copy data to buffer
                memcpy(client->response_buffer + client->response_len, evt->data, evt->data_len);
                client->response_len += evt->data_len;
                client->response_buffer[client->response_len] = '\0';
            }
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

int32_t pal_http_client_init(const pal_http_client_config_t* config, 
                               pal_http_client_handle_t* handle)
{
    if (config == NULL || config->url == NULL || handle == NULL) {
        return -1;
    }

    // Allocate internal structure
    pal_http_client_internal_t* client = (pal_http_client_internal_t*)calloc(1, sizeof(pal_http_client_internal_t));
    if (client == NULL) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to allocate HTTP client");
        return -1;
    }

    // Allocate initial response buffer
    client->response_capacity = 1024;
    client->response_buffer = (char*)malloc(client->response_capacity);
    if (client->response_buffer == NULL) {
        free(client);
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to allocate response buffer");
        return -1;
    }
    client->response_len = 0;
    client->response_buffer[0] = '\0';

    // Configure ESP HTTP client
    esp_http_client_config_t esp_config = {0};
    esp_config.url = config->url;
    esp_config.event_handler = _http_event_handler;
    esp_config.user_data = client;
    esp_config.timeout_ms = (config->timeout_ms > 0) ? config->timeout_ms : 5000;
    esp_config.cert_pem = config->cert_pem;
    esp_config.keep_alive_enable = config->keep_alive;
    esp_config.buffer_size = 1024;
    esp_config.buffer_size_tx = 1024;
    esp_config.disable_auto_redirect = false;
    esp_config.max_redirection_count = 10;

    // Use the built-in certificate bundle when no custom CA is provided.
    // This avoids allocating a per-connection CA copy in heap, which is a
    // significant source of fragmentation when requests are retried frequently.
    if (config->cert_pem == NULL) {
        esp_config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    // IMPORTANT: Disable authentication to allow custom auth headers
    // This prevents ESP-IDF from rejecting unknown auth methods like "SAS-AC1"
    esp_config.auth_type = HTTP_AUTH_TYPE_NONE;

    LOG_MSG_INFO(HTTP_ERROR_LOG_EN, "free heap before http init: %lu B", (unsigned long)esp_get_free_heap_size());
    client->esp_client = esp_http_client_init(&esp_config);
    if (client->esp_client == NULL) {
        free(client->response_buffer);
        free(client);
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to initialize ESP HTTP client (free heap: %lu B)",
                      (unsigned long)esp_get_free_heap_size());
        return -1;
    }

    *handle = (pal_http_client_handle_t)client;
    return 0;
}

int32_t pal_http_client_set_url(pal_http_client_handle_t handle, const char* url)
{
    if (handle == NULL || url == NULL) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;
    esp_err_t err = esp_http_client_set_url(client->esp_client, url);
    
    return (err == ESP_OK) ? 0 : -1;
}

int32_t pal_http_client_set_header(pal_http_client_handle_t handle, 
                                     const char* key, 
                                     const char* value)
{
    if (handle == NULL || key == NULL || value == NULL) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;
    esp_err_t err = esp_http_client_set_header(client->esp_client, key, value);
    
    return (err == ESP_OK) ? 0 : -1;
}

int32_t pal_http_client_clear_headers(pal_http_client_handle_t handle)
{
    if (handle == NULL) return -1;
    // esp_http_client_set_header() replaces existing headers with the same key,
    // so accumulated duplicates are not possible. Nothing to do here.
    return 0;
}

int32_t pal_http_client_collect_headers(pal_http_client_handle_t handle,
                                          const char** header_keys,
                                          uint8_t count)
{
    if (handle == NULL || header_keys == NULL || count > PAL_HTTP_MAX_HEADERS) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;
    
    // Clear existing headers
    for (uint8_t i = 0; i < client->header_count; i++) {
        if (client->header_keys[i] != NULL) {
            free(client->header_keys[i]);
            client->header_keys[i] = NULL;
        }
        if (client->header_values[i] != NULL) {
            free(client->header_values[i]);
            client->header_values[i] = NULL;
        }
    }
    
    // Store header keys for later capture
    client->header_count = count;
    for (uint8_t i = 0; i < count; i++) {
        client->header_keys[i] = strdup(header_keys[i]);
        client->header_values[i] = NULL;  // Will be filled in event handler
    }
    
    return 0;
}

int32_t pal_http_client_get(pal_http_client_handle_t handle, 
                              pal_http_response_t* response)
{
    if (handle == NULL) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;
    
    // Reset response buffer
    client->response_len = 0;
    client->response_buffer[0] = '\0';
    
    // Set method to GET
    esp_http_client_set_method(client->esp_client, HTTP_METHOD_GET);
    
    // Perform request - don't fail on non-200 status codes
    esp_err_t err = esp_http_client_perform(client->esp_client);
    
    // Get status code regardless of error
    int status_code = esp_http_client_get_status_code(client->esp_client);
    
    if (err != ESP_OK && status_code < 200) {
        // Only fail if we couldn't connect at all
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "HTTP GET request failed: %s", esp_err_to_name(err));
        return -1;
    }
    
    if (response != NULL) {
        response->status_code = status_code;
        response->body_len = client->response_len;
        response->body = (char*)malloc(client->response_len + 1);
        if (response->body != NULL) {
            memcpy(response->body, client->response_buffer, client->response_len);
            response->body[client->response_len] = '\0';
        }
        
        // Headers are already captured in the event handler
        response->header_count = 0;
    }

    return status_code;
}

int32_t pal_http_client_post(pal_http_client_handle_t handle,
                               const char* body,
                               size_t body_len,
                               pal_http_response_t* response)
{
    if (handle == NULL || body == NULL) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;
    
    // Reset response buffer
    client->response_len = 0;
    client->response_buffer[0] = '\0';
    
    // Set method to POST
    esp_http_client_set_method(client->esp_client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client->esp_client, body, body_len);
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client->esp_client);
    if (err != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "HTTP POST request failed");
        return -1;
    }

    int status_code = esp_http_client_get_status_code(client->esp_client);
    
    if (response != NULL) {
        response->status_code = status_code;
        response->body_len = client->response_len;
        response->body = (char*)malloc(client->response_len + 1);
        if (response->body != NULL) {
            memcpy(response->body, client->response_buffer, client->response_len);
            response->body[client->response_len] = '\0';
        }
        response->header_count = 0;
    }

    return status_code;
}

int32_t pal_http_client_get_header(pal_http_client_handle_t handle,
                                     const char* key,
                                     char* value,
                                     size_t value_len)
{
    if (handle == NULL || key == NULL || value == NULL) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;
    
    LOG_MSG_DEBUG(HTTP_ERROR_LOG_EN, "Getting header: %s", key);
    
    // Search in our captured headers
    for (uint8_t i = 0; i < client->header_count; i++) {
        if (client->header_keys[i] != NULL && 
            strcasecmp(key, client->header_keys[i]) == 0) {
            if (client->header_values[i] != NULL) {
                LOG_MSG_DEBUG(HTTP_ERROR_LOG_EN, "Header found: %s = %s", key, client->header_values[i]);
                size_t copy_len = strlen(client->header_values[i]);
                if (copy_len >= value_len) {
                    copy_len = value_len - 1;
                }
                memcpy(value, client->header_values[i], copy_len);
                value[copy_len] = '\0';
                return 0;
            }
        }
    }
    
    LOG_MSG_DEBUG(HTTP_ERROR_LOG_EN, "Header not found: %s", key);
    return -1;
}

int32_t pal_http_client_get_stream(pal_http_client_handle_t   handle,
                                    pal_http_stream_chunk_cb_t chunk_cb,
                                    void*                      user_ctx)
{
    if (handle == NULL || chunk_cb == NULL) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;

    // Configure streaming mode
    client->stream_chunk_cb  = chunk_cb;
    client->stream_user_ctx  = user_ctx;
    client->stream_abort     = false;

    // Buffered response not used in streaming mode
    client->response_len = 0;
    if (client->response_buffer) {
        client->response_buffer[0] = '\0';
    }

    esp_http_client_set_method(client->esp_client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_perform(client->esp_client);
    int status_code = esp_http_client_get_status_code(client->esp_client);

    // Clear streaming callbacks so subsequent buffered calls work normally
    client->stream_chunk_cb = NULL;
    client->stream_user_ctx = NULL;

    if (client->stream_abort) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Streaming GET aborted by chunk callback");
        return -1;
    }

    if (err != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "HTTP streaming GET failed: %s", esp_err_to_name(err));
        return -1;
    }

    return status_code;
}

int32_t pal_http_client_get_response_body(pal_http_client_handle_t handle,
                                            char* buffer,
                                            size_t buffer_len)
{
    if (handle == NULL || buffer == NULL) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;
    
    size_t copy_len = client->response_len;
    if (copy_len >= buffer_len) {
        copy_len = buffer_len - 1;
    }
    
    memcpy(buffer, client->response_buffer, copy_len);
    buffer[copy_len] = '\0';
    
    return copy_len;
}

void pal_http_response_free(pal_http_response_t* response)
{
    if (response == NULL) {
        return;
    }

    if (response->body != NULL) {
        free(response->body);
        response->body = NULL;
    }

    for (uint8_t i = 0; i < response->header_count; i++) {
        if (response->headers[i] != NULL) {
            free(response->headers[i]);
            response->headers[i] = NULL;
        }
    }
    response->header_count = 0;
}

int32_t pal_http_client_cleanup(pal_http_client_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }

    pal_http_client_internal_t* client = (pal_http_client_internal_t*)handle;
    
    // Cleanup ESP HTTP client
    if (client->esp_client != NULL) {
        esp_http_client_cleanup(client->esp_client);
    }
    
    // Free response buffer
    if (client->response_buffer != NULL) {
        free(client->response_buffer);
    }
    
    // Free collected headers
    for (uint8_t i = 0; i < client->header_count; i++) {
        if (client->header_keys[i] != NULL) {
            free(client->header_keys[i]);
        }
        if (client->header_values[i] != NULL) {
            free(client->header_values[i]);
        }
    }
    
    free(client);
    
    return 0;
}
