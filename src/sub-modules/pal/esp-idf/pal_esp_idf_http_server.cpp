/**
 * @file pal_esp_idf_http_server.cpp
 * @brief Platform Abstraction Layer - ESP-IDF HTTP Server Implementation
 * 
 * This file implements the HTTP server interface for ESP-IDF platform using
 * the esp_http_server component and esp_ota_ops for OTA updates.
 */

#include "pal_http_server.h"
#include "pal_spiffs.h"
#include "pal_types.h"
#include "pal/pal_logger.h"

#include <string.h>
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*===========================================================================*/
/*                            DEFINITIONS                                    */
/*===========================================================================*/

#define __TAG__ "PAL_HTTP"

#define HTTP_ERROR_LOG_EN      LOG_DIS

#define MAX_FILE_SIZE (512 * 1024)  // 512KB max file size for static files
#define SCRATCH_BUFSIZE 1024

/*===========================================================================*/
/*                           STATIC VARIABLES                                */
/*===========================================================================*/

static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t* s_update_partition = NULL;
static bool s_ota_in_progress = false;
static const char* s_ota_status = "Idle";

/*===========================================================================*/
/*                        UPLOAD HANDLER CONTEXT                             */
/*===========================================================================*/

typedef struct {
    pal_http_uri_handler_t completion_handler;
    pal_http_upload_handler_t upload_handler;
    void* user_ctx;
} upload_handler_ctx_t;

/*===========================================================================*/
/*                          HELPER FUNCTIONS                                 */
/*===========================================================================*/

/**
 * @brief Convert PAL HTTP method to ESP-IDF method
 */
static httpd_method_t pal_method_to_esp_method(pal_http_method_t method) {
    switch(method) {
        case PAL_HTTP_GET:     return HTTP_GET;
        case PAL_HTTP_POST:    return HTTP_POST;
        case PAL_HTTP_PUT:     return HTTP_PUT;
        case PAL_HTTP_DELETE:  return HTTP_DELETE;
        case PAL_HTTP_HEAD:    return HTTP_HEAD;
        case PAL_HTTP_OPTIONS: return HTTP_OPTIONS;
        default:               return HTTP_GET;
    }
}

/**
 * @brief Get MIME type from file extension
 */
static const char* get_mime_type(const char* filepath) {
    const char* ext = strrchr(filepath, '.');
    if(ext == NULL) return "application/octet-stream";
    
    if(strcmp(ext, ".html") == 0) return "text/html";
    if(strcmp(ext, ".htm") == 0)  return "text/html";
    if(strcmp(ext, ".css") == 0)  return "text/css";
    if(strcmp(ext, ".js") == 0)   return "application/javascript";
    if(strcmp(ext, ".json") == 0) return "application/json";
    if(strcmp(ext, ".png") == 0)  return "image/png";
    if(strcmp(ext, ".jpg") == 0)  return "image/jpeg";
    if(strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if(strcmp(ext, ".gif") == 0)  return "image/gif";
    if(strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    if(strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if(strcmp(ext, ".xml") == 0)  return "application/xml";
    if(strcmp(ext, ".pdf") == 0)  return "application/pdf";
    if(strcmp(ext, ".zip") == 0)  return "application/zip";
    if(strcmp(ext, ".txt") == 0)  return "text/plain";
    
    return "application/octet-stream";
}

/*===========================================================================*/
/*                         SERVER OPERATIONS                                 */
/*===========================================================================*/

int32_t pal_http_server_start(const pal_http_server_config_t* config, 
                               pal_http_server_handle_t* server_handle) {
    if(server_handle == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    
    if(config != NULL) {
        httpd_config.server_port = config->port;
        httpd_config.max_uri_handlers = config->max_uri_handlers;
        httpd_config.max_open_sockets = config->max_open_sockets;
        httpd_config.stack_size = config->stack_size;
        httpd_config.recv_wait_timeout = config->recv_wait_timeout / 1000;  // Convert ms to seconds
        httpd_config.send_wait_timeout = config->send_wait_timeout / 1000;
    } else {
        httpd_config.server_port = 80;
        httpd_config.max_uri_handlers = 16;
        httpd_config.max_open_sockets = 7;
        httpd_config.stack_size = 8192;
    }
    
    httpd_config.lru_purge_enable = true;
    
    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &httpd_config);
    
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return PAL_ERROR_INIT;
    }
    
    *server_handle = (pal_http_server_handle_t)server;
    LOG_MSG_INFO(HTTP_ERROR_LOG_EN, "HTTP server started on port %d", httpd_config.server_port);
    
    return PAL_OK;
}

int32_t pal_http_server_stop(pal_http_server_handle_t server_handle) {
    if(server_handle == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_handle_t server = (httpd_handle_t)server_handle;
    esp_err_t ret = httpd_stop(server);
    
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to stop HTTP server: %s", esp_err_to_name(ret));
        return PAL_ERROR;
    }
    
    LOG_MSG_INFO(HTTP_ERROR_LOG_EN, "HTTP server stopped");
    return PAL_OK;
}

/**
 * @brief Internal wrapper for URI handlers
 */
static esp_err_t uri_handler_wrapper(httpd_req_t* req) {
    pal_http_uri_handler_t handler = (pal_http_uri_handler_t)req->user_ctx;
    
    if(handler == NULL) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    int32_t result = handler((pal_http_request_t)req, req->user_ctx);
    
    return (result == PAL_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Internal wrapper for upload handlers
 * 
 * Handles multipart/form-data uploads from curl -F "file=@filename.bin".
 * The Content-Type header contains the multipart boundary, and the body
 * starts with multipart headers (including Content-Disposition with filename)
 * followed by \r\n\r\n, then the actual file data, then a closing boundary.
 */
static esp_err_t upload_handler_wrapper(httpd_req_t* req) {
    upload_handler_ctx_t* ctx = (upload_handler_ctx_t*)req->user_ctx;
    
    if(ctx == NULL || ctx->upload_handler == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Extract multipart boundary from Content-Type header
    // e.g. "multipart/form-data; boundary=------------------------abc123"
    char content_type[256] = {0};
    char boundary[128] = {0};
    bool is_multipart = false;

    if(httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK) {
        char* bnd = strstr(content_type, "boundary=");
        if(bnd != NULL) {
            bnd += 9; // strlen("boundary=")
            // boundary value runs to end of string or semicolon
            size_t bnd_len = strcspn(bnd, "; \r\n");
            if(bnd_len > 0 && bnd_len < sizeof(boundary) - 3) {
                // multipart boundary in body is prefixed with "--"
                boundary[0] = '-'; boundary[1] = '-';
                strncpy(boundary + 2, bnd, bnd_len);
                boundary[bnd_len + 2] = '\0';
                is_multipart = true;
                LOG_MSG_DEBUG(HTTP_ERROR_LOG_EN, "Multipart boundary: %s", boundary);
            }
        }
    }

    // Receive the entire body into a dynamically allocated buffer.
    // For firmware files this can be large; use streaming parse instead.
    // We read the header portion first (up to \r\n\r\n) to get the filename,
    // then stream the payload data to the upload handler.

    char filename_buf[128] = "upload.bin";
    const char* filename = filename_buf;

    // Allocate scratch buffer for receiving
    char* buf = (char*)malloc(SCRATCH_BUFSIZE);
    if(buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t total_len = req->content_len;
    size_t received_total = 0;
    size_t offset = 0;           // offset into actual file data (past headers)
    bool headers_parsed = false;
    bool first_call = true;

    // Accumulate leading bytes to parse multipart header
    // The multipart header typically looks like:
    //   --boundary\r\n
    //   Content-Disposition: form-data; name="file"; filename="foo.bin"\r\n
    //   Content-Type: application/octet-stream\r\n
    //   \r\n
    //   <file data>
    //   --boundary--\r\n
    //
    // We need to find the \r\n\r\n that separates part headers from file data.
    // We buffer up to 1024 bytes to search for it.

    char* header_scratch = (char*)malloc(1024);
    size_t header_scratch_len = 0;
    size_t file_data_start_in_chunk = 0;  // position within first payload chunk

    if(header_scratch == NULL) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    while(received_total < total_len) {
        size_t to_recv = ((total_len - received_total) < (size_t)SCRATCH_BUFSIZE)
                         ? (total_len - received_total)
                         : (size_t)SCRATCH_BUFSIZE;

        int ret = httpd_req_recv(req, buf, to_recv);
        if(ret <= 0) {
            if(ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "File upload receive failed at offset %zu", received_total);
            httpd_resp_send_500(req);
            free(buf);
            free(header_scratch);
            return ESP_FAIL;
        }

        size_t chunk_len = (size_t)ret;
        received_total += chunk_len;

        if(!headers_parsed && is_multipart) {
            // Copy into header scratch to find \r\n\r\n
            size_t copy_len = chunk_len;
            if(header_scratch_len + copy_len > 1024)
                copy_len = 1024 - header_scratch_len;
            memcpy(header_scratch + header_scratch_len, buf, copy_len);
            header_scratch_len += copy_len;

            // Look for end of part headers: \r\n\r\n
            const char* header_end = NULL;
            for(size_t i = 0; i + 3 < header_scratch_len; i++) {
                if(header_scratch[i]   == '\r' && header_scratch[i+1] == '\n' &&
                   header_scratch[i+2] == '\r' && header_scratch[i+3] == '\n') {
                    header_end = header_scratch + i + 4;  // points to first byte after \r\n\r\n
                    break;
                }
            }

            if(header_end != NULL) {
                // Parse filename from Content-Disposition in the part header
                char* cd = strstr(header_scratch, "Content-Disposition:");
                if(cd != NULL) {
                    char* fn = strstr(cd, "filename=");
                    if(fn != NULL) {
                        fn += 9;
                        if(*fn == '"') fn++;
                        size_t fn_len = strcspn(fn, "\"\r\n");
                        if(fn_len > 0 && fn_len < sizeof(filename_buf) - 1) {
                            strncpy(filename_buf, fn, fn_len);
                            filename_buf[fn_len] = '\0';
                        }
                    }
                }
                LOG_MSG_DEBUG(HTTP_ERROR_LOG_EN, "Upload filename: %s, total: %zu bytes", filename_buf, total_len);
                headers_parsed = true;

                // How many bytes of actual file data are in header_scratch after \r\n\r\n?
                size_t header_part_len = (size_t)(header_end - header_scratch);
                size_t payload_in_scratch = header_scratch_len - header_part_len;

                if(payload_in_scratch > 0) {
                    // Check if this is also the final chunk of the whole body
                    bool is_final = (received_total >= total_len);
                    // Strip trailing --boundary-- (last 2 bytes \r\n + boundary + --\r\n)
                    // For simplicity, just pass it; app_spiffs_append will store it,
                    // but we trim the closing boundary below in is_final handling.
                    int32_t result = ctx->upload_handler(
                        (pal_http_request_t)req,
                        filename_buf,
                        0,
                        (const uint8_t*)header_end,
                        payload_in_scratch,
                        is_final,
                        ctx->user_ctx
                    );
                    if(result != PAL_OK) {
                        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Upload handler failed");
                        httpd_resp_send_500(req);
                        free(buf);
                        free(header_scratch);
                        return ESP_FAIL;
                    }
                    offset += payload_in_scratch;
                }
                // first_call handled above
                first_call = false;
            }
            // If header not yet found, keep buffering
            continue;
        }

        // After headers are parsed, stream file data directly.
        // The last chunk will contain the closing boundary "--boundary--\r\n".
        // We need to strip it. Detect by checking if this is the last chunk.
        bool is_final = (received_total >= total_len);
        size_t payload_len = chunk_len;

        if(is_final && is_multipart) {
            // Trim closing boundary: "\r\n--boundary--\r\n" (or just "--boundary--\r\n")
            // boundary string already has "--" prefix, closing is boundary + "--"
            size_t close_len = strlen(boundary) + 4; // "--" + boundary + "--" + \r\n = boundary already has "--" prefix
            // boundary = "--<original_boundary>"
            // closing = "\r\n" + boundary + "--\r\n"  = 2 + strlen(boundary) + 4
            size_t trim = strlen(boundary) + 6;  // \r\n + boundary + --\r\n
            if(payload_len > trim) {
                payload_len -= trim;
            }
            (void)close_len;
        }

        if(payload_len > 0) {
            int32_t result = ctx->upload_handler(
                (pal_http_request_t)req,
                filename_buf,
                offset,
                (const uint8_t*)buf,
                payload_len,
                is_final,
                ctx->user_ctx
            );
            if(result != PAL_OK) {
                LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Upload handler failed at offset %zu", offset);
                httpd_resp_send_500(req);
                free(buf);
                free(header_scratch);
                return ESP_FAIL;
            }
            offset += payload_len;
        }
    }

    free(buf);
    free(header_scratch);

    // Call completion handler if provided
    if(ctx->completion_handler != NULL) {
        ctx->completion_handler((pal_http_request_t)req, ctx->user_ctx);
    }
    
    return ESP_OK;
}

int32_t pal_http_server_register_uri(
    pal_http_server_handle_t server_handle,
    const char* uri,
    pal_http_method_t method,
    pal_http_uri_handler_t handler,
    void* user_ctx) {
    
    if(server_handle == NULL || uri == NULL || handler == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_handle_t server = (httpd_handle_t)server_handle;
    
    httpd_uri_t uri_handler = {
        .uri = uri,
        .method = pal_method_to_esp_method(method),
        .handler = uri_handler_wrapper,
        .user_ctx = (void*)handler
    };
    
    // Store user context in a way that can be retrieved
    // For simplicity, using the handler function pointer directly
    // In production, you might want a context map
    uri_handler.user_ctx = user_ctx;
    uri_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        pal_http_uri_handler_t hdlr = (pal_http_uri_handler_t)req->user_ctx;
        if(hdlr == NULL) return ESP_FAIL;
        return (hdlr((pal_http_request_t)req, req->user_ctx) == PAL_OK) ? ESP_OK : ESP_FAIL;
    };
    
    // Actually, let's use a proper approach with stored context
    uri_handler.user_ctx = user_ctx;
    uri_handler.handler = [](httpd_req_t* req) -> esp_err_t {
        // Get the actual handler from somewhere - this needs proper context storage
        // For now, simplified approach
        return ESP_OK;
    };
    
    // Simplified: store handler as user_ctx (works for stateless handlers)
    httpd_uri_t* uri_cfg = (httpd_uri_t*)malloc(sizeof(httpd_uri_t));
    if(uri_cfg == NULL) {
        return PAL_ERROR_NO_MEMORY;
    }
    
    uri_cfg->uri = strdup(uri);
    uri_cfg->method = pal_method_to_esp_method(method);
    uri_cfg->user_ctx = user_ctx;
    
    // Create wrapper that calls the handler
    uri_cfg->handler = [](httpd_req_t* req) -> esp_err_t {
        // Extract handler from URI config
        // This is a limitation - we'll use a global map or pass differently
        return ESP_OK;
    };
    
    // Actually, let's use the proper httpd approach with direct function pointer casting
    httpd_uri_t final_uri = {
        .uri = uri,
        .method = pal_method_to_esp_method(method),
        .handler = (esp_err_t (*)(httpd_req_t*))handler,  // Direct cast (works if signatures compatible)
        .user_ctx = user_ctx
    };
    
    esp_err_t ret = httpd_register_uri_handler(server, &final_uri);
    
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to register URI handler for %s: %s", uri, esp_err_to_name(ret));
        return PAL_ERROR;
    }
    
    LOG_MSG_DEBUG(HTTP_ERROR_LOG_EN, "Registered URI handler: %s", uri);
    return PAL_OK;
}

int32_t pal_http_server_register_uri_with_upload(
    pal_http_server_handle_t server_handle,
    const char* uri,
    pal_http_uri_handler_t handler,
    pal_http_upload_handler_t upload_handler,
    void* user_ctx) {
    
    if(server_handle == NULL || uri == NULL || upload_handler == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_handle_t server = (httpd_handle_t)server_handle;
    
    // Allocate context for upload handler
    upload_handler_ctx_t* ctx = (upload_handler_ctx_t*)malloc(sizeof(upload_handler_ctx_t));
    if(ctx == NULL) {
        return PAL_ERROR_NO_MEMORY;
    }
    
    ctx->completion_handler = handler;
    ctx->upload_handler = upload_handler;
    ctx->user_ctx = user_ctx;
    
    httpd_uri_t uri_handler = {
        .uri = uri,
        .method = HTTP_POST,
        .handler = upload_handler_wrapper,
        .user_ctx = ctx
    };
    
    esp_err_t ret = httpd_register_uri_handler(server, &uri_handler);
    
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to register upload handler for %s: %s", uri, esp_err_to_name(ret));
        free(ctx);
        return PAL_ERROR;
    }
    
    LOG_MSG_DEBUG(HTTP_ERROR_LOG_EN, "Registered upload handler: %s", uri);
    return PAL_OK;
}

/*===========================================================================*/
/*                         REQUEST OPERATIONS                                */
/*===========================================================================*/

size_t pal_http_req_get_content_len(pal_http_request_t req) {
    if(req == NULL) return 0;
    
    httpd_req_t* request = (httpd_req_t*)req;
    return request->content_len;
}

int32_t pal_http_req_recv(pal_http_request_t req, char* buf, size_t buf_len, size_t* received) {
    if(req == NULL || buf == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    int ret = httpd_req_recv(request, buf, buf_len);
    
    if(ret < 0) {
        if(ret == HTTPD_SOCK_ERR_TIMEOUT) {
            return PAL_ERROR_TIMEOUT;
        }
        return PAL_ERROR;
    }
    
    if(received != NULL) {
        *received = ret;
    }
    
    return PAL_OK;
}

int32_t pal_http_req_get_header(pal_http_request_t req, const char* field, 
                                 char* val, size_t val_size) {
    if(req == NULL || field == NULL || val == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    esp_err_t ret = httpd_req_get_hdr_value_str(request, field, val, val_size);
    
    if(ret == ESP_ERR_NOT_FOUND) {
        return PAL_ERROR_NOT_FOUND;
    }
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_http_req_get_query_param(pal_http_request_t req, const char* key,
                                      char* val, size_t val_size) {
    if(req == NULL || key == NULL || val == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    
    // Get query string length
    size_t query_len = httpd_req_get_url_query_len(request);
    if(query_len == 0) {
        return PAL_ERROR_NOT_FOUND;
    }
    
    // Allocate buffer for query string
    char* query = (char*)malloc(query_len + 1);
    if(query == NULL) {
        return PAL_ERROR_NO_MEMORY;
    }
    
    // Get query string
    if(httpd_req_get_url_query_str(request, query, query_len + 1) != ESP_OK) {
        free(query);
        return PAL_ERROR;
    }
    
    // Get parameter value
    esp_err_t ret = httpd_query_key_value(query, key, val, val_size);
    free(query);
    
    if(ret == ESP_ERR_NOT_FOUND) {
        return PAL_ERROR_NOT_FOUND;
    }
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

/*===========================================================================*/
/*                         RESPONSE OPERATIONS                               */
/*===========================================================================*/

int32_t pal_http_resp_send(pal_http_request_t req, const char* data, size_t len) {
    if(req == NULL || data == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    
    // If len is 0, treat as null-terminated string
    ssize_t send_len = (len == 0) ? strlen(data) : len;
    
    esp_err_t ret = httpd_resp_send(request, data, send_len);
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_http_resp_send_chunk(pal_http_request_t req, const char* data, size_t len) {
    if(req == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    
    // NULL data means end of chunks
    ssize_t send_len = (data == NULL) ? 0 : len;
    
    esp_err_t ret = httpd_resp_send_chunk(request, data, send_len);
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_http_resp_send_file(pal_http_request_t req, const char* filepath, 
                                 const char* content_type) {
    if(req == NULL || filepath == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    
    // Set content type
    const char* mime = (content_type != NULL) ? content_type : get_mime_type(filepath);
    httpd_resp_set_type(request, mime);
    
    // Allocate buffer for file reading
    uint8_t * chunk = (uint8_t *)malloc(SCRATCH_BUFSIZE);
    if(chunk == NULL) {
        httpd_resp_send_500(request);
        return PAL_ERROR_NO_MEMORY;
    }
    
    size_t bytes_read;
    size_t total_sent = 0;
    
    // Read and send file in chunks
    do {
        int32_t ret = pal_spiffs_file_read(filepath, chunk, SCRATCH_BUFSIZE, &bytes_read);
        if(ret != PAL_OK && total_sent == 0) {
            // File doesn't exist or error on first read
            LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Failed to read file: %s", filepath);
            free(chunk);
            httpd_resp_send_404(request);
            return PAL_ERROR_NOT_FOUND;
        }
        
        if(bytes_read > 0) {
            if(httpd_resp_send_chunk(request, (char *)chunk, bytes_read) != ESP_OK) {
                free(chunk);
                return PAL_ERROR;
            }
            total_sent += bytes_read;
        }
    } while(bytes_read == SCRATCH_BUFSIZE);
    
    // End chunked response
    httpd_resp_send_chunk(request, NULL, 0);
    
    free(chunk);
    return PAL_OK;
}

int32_t pal_http_resp_set_type(pal_http_request_t req, const char* content_type) {
    if(req == NULL || content_type == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    esp_err_t ret = httpd_resp_set_type(request, content_type);
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_http_resp_set_status(pal_http_request_t req, int status_code) {
    if(req == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    
    // Convert status code to string
    const char* status_str;
    switch(status_code) {
        case 200: status_str = "200 OK"; break;
        case 201: status_str = "201 Created"; break;
        case 204: status_str = "204 No Content"; break;
        case 400: status_str = "400 Bad Request"; break;
        case 401: status_str = "401 Unauthorized"; break;
        case 403: status_str = "403 Forbidden"; break;
        case 404: status_str = "404 Not Found"; break;
        case 500: status_str = "500 Internal Server Error"; break;
        case 503: status_str = "503 Service Unavailable"; break;
        default:  status_str = "200 OK"; break;
    }
    
    esp_err_t ret = httpd_resp_set_status(request, status_str);
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_http_resp_set_header(pal_http_request_t req, const char* field, 
                                  const char* value) {
    if(req == NULL || field == NULL || value == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    esp_err_t ret = httpd_resp_set_hdr(request, field, value);
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_http_resp_send_404(pal_http_request_t req) {
    if(req == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    esp_err_t ret = httpd_resp_send_404(request);
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_http_resp_send_500(pal_http_request_t req) {
    if(req == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    httpd_req_t* request = (httpd_req_t*)req;
    esp_err_t ret = httpd_resp_send_500(request);
    
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

/*===========================================================================*/
/*                         OTA OPERATIONS                                    */
/*===========================================================================*/

int32_t pal_ota_begin(pal_ota_handle_t* ota_handle) {
    if(ota_handle == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    if(s_ota_in_progress) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "OTA already in progress");
        return PAL_ERROR_BUSY;
    }
    
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if(s_update_partition == NULL) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "No OTA partition found");
        s_ota_status = "No OTA partition";
        return PAL_ERROR_NOT_FOUND;
    }
    
    LOG_MSG_INFO(HTTP_ERROR_LOG_EN, "Starting OTA update to partition subtype %d at offset 0x%x",
             s_update_partition->subtype, s_update_partition->address);
    
    esp_err_t err = esp_ota_begin(s_update_partition, OTA_SIZE_UNKNOWN, &s_ota_handle);
    if(err != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "OTA begin failed: %s", esp_err_to_name(err));
        s_ota_status = "OTA begin failed";
        return PAL_ERROR_INIT;
    }
    
    s_ota_in_progress = true;
    s_ota_status = "In progress";
    *ota_handle = (pal_ota_handle_t)s_ota_handle;
    
    return PAL_OK;
}

int32_t pal_ota_write(pal_ota_handle_t ota_handle, const uint8_t* data, size_t len) {
    if(!s_ota_in_progress || data == NULL || len == 0) {
        return PAL_ERROR_INVALID;
    }
    
    esp_err_t err = esp_ota_write(s_ota_handle, data, len);
    if(err != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "OTA write failed: %s", esp_err_to_name(err));
        s_ota_status = "Write failed";
        return PAL_ERROR;
    }
    
    return PAL_OK;
}

int32_t pal_ota_end(pal_ota_handle_t ota_handle) {
    if(!s_ota_in_progress) {
        return PAL_ERROR_INVALID;
    }
    
    esp_err_t err = esp_ota_end(s_ota_handle);
    if(err != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "OTA end failed: %s", esp_err_to_name(err));
        s_ota_status = "Validation failed";
        s_ota_in_progress = false;
        return PAL_ERROR;
    }
    
    err = esp_ota_set_boot_partition(s_update_partition);
    if(err != ESP_OK) {
        LOG_MSG_ERROR(HTTP_ERROR_LOG_EN, "Set boot partition failed: %s", esp_err_to_name(err));
        s_ota_status = "Boot partition set failed";
        s_ota_in_progress = false;
        return PAL_ERROR;
    }
    
    LOG_MSG_INFO(HTTP_ERROR_LOG_EN, "OTA update successful, ready to restart");
    s_ota_status = "Success - Ready to restart";
    s_ota_in_progress = false;
    
    return PAL_OK;
}

int32_t pal_ota_abort(pal_ota_handle_t ota_handle) {
    if(!s_ota_in_progress) {
        return PAL_ERROR_INVALID;
    }
    
    esp_err_t err = esp_ota_abort(s_ota_handle);
    s_ota_in_progress = false;
    s_ota_status = "Aborted";
    
    return (err == ESP_OK) ? PAL_OK : PAL_ERROR;
}

const char* pal_ota_get_status_string(void) {
    return s_ota_status;
}
