/**
 * @file pal_http_client.h
 * @brief Platform Abstraction Layer for HTTP/HTTPS Client operations
 * 
 * This header provides a platform-independent interface for HTTP/HTTPS client
 * functionality including GET/POST requests, TLS support, and header management.
 */

#ifndef PAL_HTTP_CLIENT_H
#define PAL_HTTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

#define PAL_HTTP_MAX_URL_LEN        512     /**< Maximum URL length */
#define PAL_HTTP_MAX_HEADER_LEN     256     /**< Maximum header length */
#define PAL_HTTP_MAX_HEADERS        10      /**< Maximum number of headers */
#define PAL_HTTP_MAX_RESPONSE_LEN   4096    /**< Maximum response buffer length */

// ============================================================================
// Type Definitions
// ============================================================================

/**
 * @brief HTTP client handle (opaque pointer)
 */
typedef void* pal_http_client_handle_t;

#include "pal_http_types.h"

/**
 * @brief HTTP client configuration
 */
typedef struct {
    const char* url;                    /**< Target URL (required) */
    const char* cert_pem;               /**< CA certificate for TLS (NULL for no TLS verification) */
    uint32_t timeout_ms;                /**< Request timeout in milliseconds (0 = default) */
    bool keep_alive;                    /**< Enable HTTP keep-alive */
} pal_http_client_config_t;

/**
 * @brief HTTP response structure
 */
typedef struct {
    int32_t status_code;                /**< HTTP status code (200, 404, etc.) */
    char* body;                         /**< Response body (caller must free) */
    size_t body_len;                    /**< Length of response body */
    char* headers[PAL_HTTP_MAX_HEADERS];/**< Response headers (caller must free each) */
    uint8_t header_count;               /**< Number of headers received */
} pal_http_response_t;

// ============================================================================
// HTTP Client Management Functions
// ============================================================================

/**
 * @brief Initialize HTTP client
 * 
 * Creates and initializes an HTTP client with the specified configuration.
 * 
 * @param config Client configuration
 * @param handle Pointer to store client handle
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_http_client_init(const pal_http_client_config_t* config, 
                               pal_http_client_handle_t* handle);

/**
 * @brief Set request URL
 * 
 * Updates the URL for the HTTP client.
 * 
 * @param handle HTTP client handle
 * @param url New URL
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_http_client_set_url(pal_http_client_handle_t handle, const char* url);

/**
 * @brief Add request header
 * 
 * Adds a header to the HTTP request.
 * 
 * @param handle HTTP client handle
 * @param key Header key (e.g., "Content-Type")
 * @param value Header value (e.g., "application/json")
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_http_client_set_header(pal_http_client_handle_t handle, 
                                     const char* key, 
                                     const char* value);

/**
 * @brief Clear all request headers previously added with pal_http_client_set_header().
 *
 * Use this when reusing a handle across multiple requests that require different
 * header sets (e.g., switching from a challenge/response auth header to Basic auth).
 *
 * @param handle HTTP client handle
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_http_client_clear_headers(pal_http_client_handle_t handle);

/**
 * @brief Enable header collection
 * 
 * Specifies which response headers to collect.
 * 
 * @param handle HTTP client handle
 * @param header_keys Array of header keys to collect
 * @param count Number of headers to collect
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_http_client_collect_headers(pal_http_client_handle_t handle,
                                          const char** header_keys,
                                          uint8_t count);

// ============================================================================
// HTTP Request Functions
// ============================================================================

/**
 * @brief Perform HTTP GET request
 * 
 * Executes a GET request and returns the response.
 * 
 * @param handle HTTP client handle
 * @param response Pointer to store response (caller must free response->body)
 * @return int32_t HTTP status code on success, negative error code on failure
 */
int32_t pal_http_client_get(pal_http_client_handle_t handle, 
                              pal_http_response_t* response);

/**
 * @brief Perform HTTP POST request
 * 
 * Executes a POST request with the provided body and returns the response.
 * 
 * @param handle HTTP client handle
 * @param body Request body data
 * @param body_len Length of request body
 * @param response Pointer to store response (caller must free response->body)
 * @return int32_t HTTP status code on success, negative error code on failure
 */
int32_t pal_http_client_post(pal_http_client_handle_t handle,
                               const char* body,
                               size_t body_len,
                               pal_http_response_t* response);

/**
 * @brief Get specific response header
 * 
 * Retrieves a specific header from the last response.
 * 
 * @param handle HTTP client handle
 * @param key Header key to retrieve
 * @param value Buffer to store header value
 * @param value_len Length of value buffer
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_http_client_get_header(pal_http_client_handle_t handle,
                                     const char* key,
                                     char* value,
                                     size_t value_len);

/**
 * @brief Chunk callback invoked during a streaming GET.
 *
 * Called repeatedly as data arrives. Return 0 to continue, non-zero to abort.
 *
 * @param data        Pointer to the received data chunk
 * @param len         Number of bytes in this chunk
 * @param user_ctx    Caller-supplied context pointer
 * @return 0 to continue, non-zero to abort the transfer
 */
typedef int32_t (*pal_http_stream_chunk_cb_t)(const uint8_t* data,
                                               size_t         len,
                                               void*          user_ctx);

/**
 * @brief Perform a streaming HTTP GET request.
 *
 * Executes a GET request and delivers the response body in chunks via
 * @p chunk_cb instead of buffering the entire response in RAM.
 * Response headers registered with pal_http_client_collect_headers() are
 * still captured and accessible via pal_http_client_get_header() afterwards.
 *
 * @param handle    HTTP client handle
 * @param chunk_cb  Callback invoked for each received data chunk
 * @param user_ctx  Passed verbatim to @p chunk_cb
 * @return HTTP status code on success (e.g. 200), negative error code on failure
 */
int32_t pal_http_client_get_stream(pal_http_client_handle_t  handle,
                                    pal_http_stream_chunk_cb_t chunk_cb,
                                    void*                      user_ctx);

/**
 * @brief Get response body as string
 * 
 * Retrieves the response body from the last request.
 * 
 * @param handle HTTP client handle
 * @param buffer Buffer to store response body
 * @param buffer_len Length of buffer
 * @return int32_t Number of bytes copied, negative error code on failure
 */
int32_t pal_http_client_get_response_body(pal_http_client_handle_t handle,
                                            char* buffer,
                                            size_t buffer_len);

// ============================================================================
// Cleanup Functions
// ============================================================================

/**
 * @brief Free HTTP response
 * 
 * Frees memory allocated for HTTP response.
 * 
 * @param response Response structure to free
 */
void pal_http_response_free(pal_http_response_t* response);

/**
 * @brief Cleanup and close HTTP client
 * 
 * Releases all resources associated with the HTTP client.
 * 
 * @param handle HTTP client handle
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_http_client_cleanup(pal_http_client_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // PAL_HTTP_CLIENT_H
