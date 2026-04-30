#ifndef HSYS_CMD_RESP_FRAME_H
#define HSYS_CMD_RESP_FRAME_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "crc32.h"  // Include your CRC32 implementation

#ifdef __cplusplus
extern "C" {
#endif

// Protocol constants
#define HSYS_STX "HSYS"
#define HSYS_PROTOCOL_VERSION 0x00
#define HSYS_MAX_DATA_SIZE 2048
#define HSYS_OTA_BUFFER_SIZE 2048
#define HSYS_MIN_FRAME_SIZE 24  // Minimum frame size without data (4+1+1+1+2+6+1+4+4)
#define HSYS_RSP_PIPE_ID_SIZE 6

// Command IDs
typedef enum {
    CMD_OTA_START = 0x0001,
    CMD_OTA_DATA = 0x0002,
    CMD_OTA_COMPLETE = 0x0003,
    CMD_OTA_GET_STATUS = 0x0004,
    CMD_GET_FW_VERSION = 0x0005,
    CMD_GET_FW_VERSION_SUB_1 = 0x0006
} HsysCommandId_t;

// OTA ID types
typedef enum {
    OTAMAIN = 0x00,
    OTASUB_1 = 0x01  // Ex - ESP07 Firmware
} HsysOtaId_t;

// Command status codes
typedef enum {
    CMD_STATUS_OK = 0x00,
    CMD_STATUS_ERROR = 0x01,
    CMD_STATUS_INVALID_COMMAND = 0x02,
    CMD_STATUS_INVALID_DATA = 0x03,
    CMD_STATUS_CRC_ERROR = 0x04,
    CMD_STATUS_OTA_ERROR = 0x05,
    CMD_STATUS_BUSY = 0x06
} HsysCmdStatus_t;

// OTA status codes
typedef enum {
    OTA_IDLE = 0x00,
    OTA_IN_PROGRESS = 0x01,
    OTA_COMPLETED = 0x02
} HsysOtaStatus_t;

// Frame structure
typedef struct {
    char startCode[4];          // STX - 4B
    uint8_t version;            // VER - 1B
    uint8_t flags;              // FLAGS - 1B
    uint8_t type;               // TYPE - 1B
    uint16_t commandId;         // CMDID - 2B
    char responsePipeId[6];     // RSP-PIPE-ID - 6B
    uint8_t sequenceNumber;     // SEQ NU - 1B
    uint32_t dataSize;          // SIZE - 4B
    uint8_t data[HSYS_MAX_DATA_SIZE];              // DATA - variable (up to 2K)
    uint32_t crc32;             // CRC32 - 4B
    bool dataAllocated;         // Internal flag for memory management
} HsysCmdRespFrame_t;

// Frame management functions
bool hsys_frame_init(HsysCmdRespFrame_t* frame);
bool hsys_frame_init_with_params(HsysCmdRespFrame_t* frame, HsysCommandId_t commandId,
                                uint8_t flags, uint8_t type,
                                const char* responsePipeId, uint8_t sequenceNumber);
bool hsys_frame_set_data(HsysCmdRespFrame_t* frame, const uint8_t* data, uint32_t size);
bool hsys_frame_serialize(const HsysCmdRespFrame_t* frame, uint8_t* buffer, 
                         uint32_t bufferSize, uint32_t* frameSize);
bool hsys_frame_deserialize(HsysCmdRespFrame_t* frame, const uint8_t* buffer, uint32_t bufferSize);
bool hsys_frame_is_valid(const HsysCmdRespFrame_t* frame);
void hsys_frame_clear(HsysCmdRespFrame_t* frame);
void hsys_frame_free(HsysCmdRespFrame_t* frame);

// Getter functions
HsysCommandId_t hsys_frame_get_command_id(const HsysCmdRespFrame_t* frame);
uint8_t hsys_frame_get_flags(const HsysCmdRespFrame_t* frame);
uint8_t hsys_frame_get_type(const HsysCmdRespFrame_t* frame);
void hsys_frame_get_response_pipe_id(const HsysCmdRespFrame_t* frame, char * response_pipe_id);
uint8_t hsys_frame_get_sequence_number(const HsysCmdRespFrame_t* frame);
const uint8_t* hsys_frame_get_data(const HsysCmdRespFrame_t* frame);
uint32_t hsys_frame_get_data_size(const HsysCmdRespFrame_t* frame);
uint32_t hsys_frame_get_crc32(const HsysCmdRespFrame_t* frame);

// Command-specific data creators
uint32_t hsys_create_ota_start_command_data(uint8_t* buffer, HsysOtaId_t otaId, uint32_t otaSize);
uint32_t hsys_create_ota_data_command_data(uint8_t* buffer, uint32_t otaOffset, 
                                          const uint8_t* otaDataBuffer, uint16_t dataSize);
uint32_t hsys_create_ota_complete_command_data(uint8_t* buffer, uint32_t otaCrc32);
uint32_t hsys_create_basic_response_data(uint8_t* buffer, HsysCmdStatus_t status);
uint32_t hsys_create_ota_data_response_data(uint8_t* buffer, uint32_t offset_expecting, HsysCmdStatus_t status);
uint32_t hsys_create_ota_status_response_data(uint8_t* buffer, HsysCmdStatus_t cmdStatus, 
                                             HsysOtaStatus_t otaStatus);
uint32_t hsys_create_fw_version_response_data(uint8_t* buffer, uint32_t fwVersion);

// Command-specific data parsers
bool hsys_parse_ota_start_command_data(const uint8_t* data, uint32_t size, 
                                      HsysOtaId_t* otaId, uint32_t* otaSize);
bool hsys_parse_ota_data_command_data(const uint8_t* data, uint32_t size, 
                                     uint32_t* otaOffset, uint8_t* otaDataBuffer, uint16_t* dataSize);
bool hsys_parse_ota_complete_command_data(const uint8_t* data, uint32_t size, uint32_t* otaCrc32);
bool hsys_parse_basic_response(const uint8_t* data, uint32_t size, HsysCmdStatus_t* status);
bool hsys_parse_ota_status_response(const uint8_t* data, uint32_t size, 
                                   HsysCmdStatus_t* cmdStatus, HsysOtaStatus_t* otaStatus);
bool hsys_parse_fw_version_response(const uint8_t* data, uint32_t size, uint32_t* fwVersion);

// Utility functions
uint32_t hsys_calculate_crc32(const uint8_t* data, uint32_t length);
void hsys_print_frame(const HsysCmdRespFrame_t* frame);  // For debugging

#ifdef __cplusplus
}
#endif

#endif // HSYS_CMD_RESP_FRAME_H
