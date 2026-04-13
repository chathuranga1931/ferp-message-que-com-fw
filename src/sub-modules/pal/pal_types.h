/**
 * @file pal_types.h
 * @brief Platform Abstraction Layer - Common Types and Error Codes
 * 
 * This file defines common types and error codes used across all PAL modules.
 * These definitions are platform-independent.
 */

#ifndef PAL_TYPES_H
#define PAL_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*===========================================================================*/
/*                            ERROR CODES                                    */
/*===========================================================================*/

// Common error codes across all PAL modules
#define PAL_OK              0       // Success
#define PAL_ERROR          -1       // General error
#define PAL_ERROR_INIT     -2       // Initialization error
#define PAL_ERROR_BUSY     -3       // Resource busy
#define PAL_ERROR_TIMEOUT  -4       // Operation timeout
#define PAL_ERROR_NOT_FOUND -5      // Resource not found
#define PAL_ERROR_NO_SPACE  -6      // No space available
#define PAL_ERROR_INVALID   -7      // Invalid parameter
#define PAL_ERROR_IO        -8      // I/O error
#define PAL_ERROR_OVERFLOW  -9      // Buffer overflow
#define PAL_ERROR_NO_MEMORY -10     // Out of memory

/*===========================================================================*/
/*                            COMMON TYPES                                   */
/*===========================================================================*/

// GPIO pin type (platform-agnostic)
typedef int32_t pal_gpio_num_t;

// Invalid GPIO number
#define PAL_GPIO_NUM_NC     -1      // Not connected

#endif // PAL_TYPES_H
