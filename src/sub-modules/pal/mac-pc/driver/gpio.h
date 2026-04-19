/**
 * @file driver/gpio.h
 * @brief Stub for ESP-IDF driver/gpio.h — used on macOS simulator builds only.
 *
 * board_inf.h includes "driver/gpio.h". On the simulator the PAL mac-pc include
 * directory is first on the include path, so this file shadows the real ESP-IDF
 * header. It provides only the types and GPIO_NUM_xx defines needed by the BSP.
 */
#ifndef DRIVER_GPIO_H_STUB
#define DRIVER_GPIO_H_STUB

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

/* ── GPIO number type ─────────────────────────────────────────────────────── */
typedef int gpio_num_t;

/* ── GPIO numbers 0 – 50 ──────────────────────────────────────────────────── */
#define GPIO_NUM_0   0
#define GPIO_NUM_1   1
#define GPIO_NUM_2   2
#define GPIO_NUM_3   3
#define GPIO_NUM_4   4
#define GPIO_NUM_5   5
#define GPIO_NUM_6   6
#define GPIO_NUM_7   7
#define GPIO_NUM_8   8
#define GPIO_NUM_9   9
#define GPIO_NUM_10  10
#define GPIO_NUM_11  11
#define GPIO_NUM_12  12
#define GPIO_NUM_13  13
#define GPIO_NUM_14  14
#define GPIO_NUM_15  15
#define GPIO_NUM_16  16
#define GPIO_NUM_17  17
#define GPIO_NUM_18  18
#define GPIO_NUM_19  19
#define GPIO_NUM_20  20
#define GPIO_NUM_21  21
#define GPIO_NUM_22  22
#define GPIO_NUM_23  23
#define GPIO_NUM_24  24
#define GPIO_NUM_25  25
#define GPIO_NUM_26  26
#define GPIO_NUM_27  27
#define GPIO_NUM_28  28
#define GPIO_NUM_29  29
#define GPIO_NUM_30  30
#define GPIO_NUM_31  31
#define GPIO_NUM_32  32
#define GPIO_NUM_33  33
#define GPIO_NUM_34  34
#define GPIO_NUM_35  35
#define GPIO_NUM_36  36
#define GPIO_NUM_37  37
#define GPIO_NUM_38  38
#define GPIO_NUM_39  39
#define GPIO_NUM_40  40
#define GPIO_NUM_41  41
#define GPIO_NUM_42  42
#define GPIO_NUM_43  43
#define GPIO_NUM_44  44
#define GPIO_NUM_45  45
#define GPIO_NUM_46  46
#define GPIO_NUM_47  47
#define GPIO_NUM_48  48
#define GPIO_NUM_49  49
#define GPIO_NUM_50  50

#define GPIO_NUM_MAX 51

/* ── esp_err_t stub (also pulled in via esp_err.h — safe to define here too) */
#ifndef ESP_ERR_T_DEFINED
#define ESP_ERR_T_DEFINED
typedef int esp_err_t;
#define ESP_OK   0
#define ESP_FAIL (-1)
#endif

#endif /* DRIVER_GPIO_H_STUB */
