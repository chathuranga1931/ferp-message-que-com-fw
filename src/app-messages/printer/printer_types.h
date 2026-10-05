// printer_types.h
//
// Shared enums for the Printer message group (0x0060 – 0x006F).

#pragma once

#include <stdint.h>

/** What MsgPrintJob asks the printer to produce. */
typedef enum : uint8_t {
    PRINT_JOB_RECEIPT   = 0,   ///< fuel receipt (theme 1/2/3 from config)
    PRINT_JOB_TOTALIZER = 1,   ///< totalizer slip
    PRINT_JOB_SAMPLE    = 2,   ///< built-in sample receipt (fields ignored)
    PRINT_JOB_INFO      = 3,   ///< boot/info slip: MAC, IP, version, bill count
} print_job_kind_t;

/** Where a print job came from (reported back in MsgPrintResult / logs). */
typedef enum : uint8_t {
    PRINT_SRC_HTTP   = 0,   ///< legacy local HTTP API (/print, /print-totalizer)
    PRINT_SRC_CLOUD  = 1,   ///< cloud print queue
    PRINT_SRC_MQTT   = 2,   ///< MQTT command (FERP Device Web / tools)
    PRINT_SRC_LOCAL  = 3,   ///< on-device (boot slip, button)
} print_job_source_t;

/** Result of one print job. */
typedef enum : uint8_t {
    PRINT_RESULT_OK          = 0,
    PRINT_RESULT_NOT_READY   = 1,   ///< transport not connected (e.g. USB printer unplugged)
    PRINT_RESULT_WRITE_FAIL  = 2,   ///< transport write error / timeout
    PRINT_RESULT_INVALID     = 3,   ///< job content rejected
    PRINT_RESULT_BUSY        = 4,   ///< could not be queued
} print_result_t;

/** MsgPrinterCmd commands. */
typedef enum : uint8_t {
    PRINTER_CMD_PRINT_SAMPLE = 0,   ///< print the built-in sample receipt
    PRINTER_CMD_RESET_COUNT  = 1,   ///< reset the bill counter to 0 (RAM + flash)
    PRINTER_CMD_PRINT_INFO   = 2,   ///< print the info slip (MAC/IP/version/count)
    PRINTER_CMD_BEEP         = 3,   ///< sound the buzzer (arg = seconds, 0 → 1 s)
} printer_cmd_t;

/** ModuleCloudPrint state (MsgCloudPrintStatus.state). */
typedef enum : uint8_t {
    CLOUD_PRINT_DISABLED     = 0,   ///< en_cloud_print = false
    CLOUD_PRINT_WAIT_NET     = 1,   ///< waiting for internet
    CLOUD_PRINT_REGISTERING  = 2,   ///< bootstrap / device-config in progress
    CLOUD_PRINT_READY        = 3,   ///< queue id known, polling on notify / timer
    CLOUD_PRINT_REG_FAILED   = 4,   ///< registration failed — retrying
    CLOUD_PRINT_NO_QUEUE     = 5,   ///< registered but the cloud returned no printer queue
} cloud_print_state_t;
