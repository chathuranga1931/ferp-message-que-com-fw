/**
 * @file pal_http_types.h
 * @brief Shared HTTP types for PAL HTTP server and client interfaces.
 *
 * Included by pal_http_server.h and pal_http_client.h to provide a single
 * definition of pal_http_method_t with values used by both layers.
 */

#ifndef PAL_HTTP_TYPES_H
#define PAL_HTTP_TYPES_H

/**
 * @brief HTTP method identifiers (shared by HTTP server and client PAL layers).
 *
 * Short-form names (PAL_HTTP_GET etc.) are used by the server layer.
 * Long-form aliases (PAL_HTTP_METHOD_GET etc.) are used by the client layer.
 */
typedef enum {
    PAL_HTTP_GET     = 0,
    PAL_HTTP_POST    = 1,
    PAL_HTTP_PUT     = 2,
    PAL_HTTP_DELETE  = 3,
    PAL_HTTP_HEAD    = 4,
    PAL_HTTP_OPTIONS = 5,
    PAL_HTTP_PATCH   = 6,

    /* Client-layer aliases */
    PAL_HTTP_METHOD_GET    = PAL_HTTP_GET,
    PAL_HTTP_METHOD_POST   = PAL_HTTP_POST,
    PAL_HTTP_METHOD_PUT    = PAL_HTTP_PUT,
    PAL_HTTP_METHOD_DELETE = PAL_HTTP_DELETE,
    PAL_HTTP_METHOD_HEAD   = PAL_HTTP_HEAD,
    PAL_HTTP_METHOD_PATCH  = PAL_HTTP_PATCH
} pal_http_method_t;

#endif /* PAL_HTTP_TYPES_H */
