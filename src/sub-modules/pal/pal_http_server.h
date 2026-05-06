/**
 * @file pal_http_server.h
 * @brief Platform Abstraction Layer - HTTP Server Interface
 * 
 * This file defines the platform-independent interface for HTTP server operations.
 * Provides abstraction for web server functionality including request handling,
 * response generation, static file serving, and OTA updates.
 */

#ifndef PAL_HTTP_SERVER_H
#define PAL_HTTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pal_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*===========================================================================*/
/*                            TYPE DEFINITIONS                               */
/*===========================================================================*/

/**
 * @brief Opaque handle for HTTP server instance
 */
typedef void* pal_http_server_handle_t;

/**
 * @brief Opaque handle for HTTP request
 */
typedef void* pal_http_request_t;

#include "pal_http_types.h"

/**
 * @brief HTTP server configuration
 */
typedef struct {
    uint16_t port;                  // Server port (default: 80)
    uint16_t max_uri_handlers;      // Maximum number of URI handlers (default: 16)
    uint16_t max_open_sockets;      // Maximum open sockets (default: 7)
    uint32_t stack_size;            // Stack size for server task (default: 8192)
    uint32_t recv_wait_timeout;     // Receive timeout in ms (default: 5000)
    uint32_t send_wait_timeout;     // Send timeout in ms (default: 5000)
} pal_http_server_config_t;

/**
 * @brief HTTP URI handler callback
 * 
 * @param req HTTP request handle
 * @param user_ctx User context passed during registration
 * @return PAL_OK on success, error code otherwise
 */
typedef int32_t (*pal_http_uri_handler_t)(pal_http_request_t req, void* user_ctx);

/**
 * @brief File upload handler callback (for chunked file uploads)
 * 
 * @param req HTTP request handle
 * @param filename Name of the uploaded file
 * @param offset Current offset in the file
 * @param data Chunk of file data
 * @param len Length of data chunk
 * @param is_final True if this is the last chunk
 * @param user_ctx User context
 * @return PAL_OK on success, error code otherwise
 */
typedef int32_t (*pal_http_upload_handler_t)(
    pal_http_request_t req,
    const char* filename,
    size_t offset,
    const uint8_t* data,
    size_t len,
    bool is_final,
    void* user_ctx
);

/*===========================================================================*/
/*                         SERVER OPERATIONS                                 */
/*===========================================================================*/

/**
 * @brief Start HTTP server with configuration
 * 
 * @param config Server configuration (NULL for defaults)
 * @param server_handle Output parameter for server handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_server_start(const pal_http_server_config_t* config, 
                               pal_http_server_handle_t* server_handle);

/**
 * @brief Stop HTTP server
 * 
 * @param server_handle Server handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_server_stop(pal_http_server_handle_t server_handle);

/**
 * @brief Register URI handler
 * 
 * @param server_handle Server handle
 * @param uri URI path (e.g., "/", "/api/config")
 * @param method HTTP method
 * @param handler Handler callback function
 * @param user_ctx User context passed to handler
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_server_register_uri(
    pal_http_server_handle_t server_handle,
    const char* uri,
    pal_http_method_t method,
    pal_http_uri_handler_t handler,
    void* user_ctx
);

/**
 * @brief Register URI handler with upload support (for POST with file upload)
 * 
 * @param server_handle Server handle
 * @param uri URI path
 * @param handler Regular handler callback (called after upload completes)
 * @param upload_handler Upload handler for chunked data
 * @param user_ctx User context
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_server_register_uri_with_upload(
    pal_http_server_handle_t server_handle,
    const char* uri,
    pal_http_uri_handler_t handler,
    pal_http_upload_handler_t upload_handler,
    void* user_ctx
);

/*===========================================================================*/
/*                         REQUEST OPERATIONS                                */
/*===========================================================================*/

/**
 * @brief Get request content length
 * 
 * @param req Request handle
 * @return Content length in bytes, 0 if no content
 */
size_t pal_http_req_get_content_len(pal_http_request_t req);

/**
 * @brief Receive request body data
 * 
 * @param req Request handle
 * @param buf Buffer to store received data
 * @param buf_len Buffer length
 * @param received Output parameter for bytes received
 * @return PAL_OK on success, PAL_ERROR_TIMEOUT on timeout, error code otherwise
 */
int32_t pal_http_req_recv(pal_http_request_t req, char* buf, size_t buf_len, size_t* received);

/**
 * @brief Get request header value
 * 
 * @param req Request handle
 * @param field Header field name (e.g., "Content-Type")
 * @param val Buffer to store header value
 * @param val_size Buffer size
 * @return PAL_OK on success, PAL_ERROR_NOT_FOUND if header not present
 */
int32_t pal_http_req_get_header(pal_http_request_t req, const char* field, 
                                 char* val, size_t val_size);

/**
 * @brief Get query parameter value
 * 
 * @param req Request handle
 * @param key Query parameter key
 * @param val Buffer to store value
 * @param val_size Buffer size
 * @return PAL_OK on success, PAL_ERROR_NOT_FOUND if parameter not present
 */
int32_t pal_http_req_get_query_param(pal_http_request_t req, const char* key,
                                      char* val, size_t val_size);

/*===========================================================================*/
/*                         RESPONSE OPERATIONS                               */
/*===========================================================================*/

/**
 * @brief Send HTTP response
 * 
 * @param req Request handle
 * @param data Response data
 * @param len Data length (use 0 for null-terminated string)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_resp_send(pal_http_request_t req, const char* data, size_t len);

/**
 * @brief Send HTTP response chunk (for streaming)
 * 
 * @param req Request handle
 * @param data Chunk data (NULL to end chunked response)
 * @param len Chunk length
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_resp_send_chunk(pal_http_request_t req, const char* data, size_t len);

/**
 * @brief Send file as HTTP response
 * 
 * @param req Request handle
 * @param filepath Path to file (relative to filesystem root)
 * @param content_type MIME content type (NULL for auto-detection)
 * @return PAL_OK on success, PAL_ERROR_NOT_FOUND if file not found
 */
int32_t pal_http_resp_send_file(pal_http_request_t req, const char* filepath, 
                                 const char* content_type);

/**
 * @brief Set response content type
 * 
 * @param req Request handle
 * @param content_type MIME content type (e.g., "text/html", "application/json")
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_resp_set_type(pal_http_request_t req, const char* content_type);

/**
 * @brief Set response status code
 * 
 * @param req Request handle
 * @param status_code HTTP status code (e.g., 200, 404, 500)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_resp_set_status(pal_http_request_t req, int status_code);

/**
 * @brief Set response header
 * 
 * @param req Request handle
 * @param field Header field name
 * @param value Header value
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_resp_set_header(pal_http_request_t req, const char* field, 
                                  const char* value);

/**
 * @brief Send 404 Not Found response
 * 
 * @param req Request handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_resp_send_404(pal_http_request_t req);

/**
 * @brief Send 500 Internal Server Error response
 * 
 * @param req Request handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_http_resp_send_500(pal_http_request_t req);

/*===========================================================================*/
/*                         OTA OPERATIONS                                    */
/*===========================================================================*/

/**
 * @brief OTA update handle
 */
typedef void* pal_ota_handle_t;

/**
 * @brief Begin OTA update
 * 
 * @param ota_handle Output parameter for OTA handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_ota_begin(pal_ota_handle_t* ota_handle);

/**
 * @brief Write OTA data
 * 
 * @param ota_handle OTA handle
 * @param data Data to write
 * @param len Data length
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_ota_write(pal_ota_handle_t ota_handle, const uint8_t* data, size_t len);

/**
 * @brief End OTA update and set boot partition
 * 
 * @param ota_handle OTA handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_ota_end(pal_ota_handle_t ota_handle);

/**
 * @brief Abort OTA update
 * 
 * @param ota_handle OTA handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_ota_abort(pal_ota_handle_t ota_handle);

/**
 * @brief Get OTA status string
 * 
 * @return Status string (e.g., "Success", "Failed", "In Progress")
 */
const char* pal_ota_get_status_string(void);

#ifdef __cplusplus
}
#endif

#endif /* PAL_HTTP_SERVER_H */
