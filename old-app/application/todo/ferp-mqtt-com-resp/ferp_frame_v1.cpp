#include "ferp_frame_v1.h"
#include "pal_logger.h"


#define __TAG__  "APP_FRV1"

// Helper function to copy string with size limit
static void copy_string(char* dest, const char* src, size_t destSize) {
    if (!dest || !src) return;
    
    strncpy(dest, src, destSize);
    dest[destSize - 1] = '\0';  // Ensure null termination
    
    // Pad with spaces if needed
    size_t len = strlen(dest);
    for (size_t i = len; i < destSize; i++) {
        dest[i] = ' ';
    }
}

// Helper function to allocate data buffer
// static bool allocate_data(HsysCmdRespFrame_t* frame, uint32_t size) {
//     // Free existing data first
//     if (frame->dataAllocated && frame->data) {
//         free(frame->data);
//         frame->data = NULL;
//         frame->dataAllocated = false;
//     }
    
//     if (size == 0) {
//         frame->data = NULL;
//         frame->dataSize = 0;
//         return true;
//     }
    
//     frame->data = (uint8_t*)malloc(size);
//     if (!frame->data) {
//         return false;
//     }
    
//     frame->dataAllocated = true;
//     return true;
// }

// Initialize frame with default values
bool hsys_frame_init(HsysCmdRespFrame_t* frame) 
{
    if (!frame) return false;
    
    memset(frame, 0, sizeof(HsysCmdRespFrame_t));
    
    // Initialize STX and version
    strncpy(frame->startCode, HSYS_STX, 4);
    frame->version = HSYS_PROTOCOL_VERSION;
    frame->dataAllocated = false;
    
    return true;
}

// Initialize frame with parameters
bool hsys_frame_init_with_params(HsysCmdRespFrame_t* frame, HsysCommandId_t commandId,
                                uint8_t flags, uint8_t type,
                                const char* responsePipeId, uint8_t sequenceNumber) {
    if (!frame) return false;
    
    if (!hsys_frame_init(frame)) {
        return false;
    }
    
    // Set flags and type
    frame->flags = flags;
    frame->type = type;

    // Set command ID
    frame->commandId = (uint16_t)commandId;
    
    // Set response pipe ID (ensure 6 characters)
    copy_string(frame->responsePipeId, responsePipeId, HSYS_RSP_PIPE_ID_SIZE);
    
    // Set sequence number
    frame->sequenceNumber = sequenceNumber;
    
    return true;
}

// Set data for the frame
bool hsys_frame_set_data(HsysCmdRespFrame_t* frame, const uint8_t* data, uint32_t size) {
    if (!frame || size > HSYS_MAX_DATA_SIZE) {
        return false;
    }
    
    // if (!allocate_data(frame, size)) {
    //     return false;
    // }
    
    if (data && size > 0) {
        memcpy(frame->data, data, size);
    }
    
    frame->dataSize = size;
    return true;
}

// Serialize frame to buffer
bool hsys_frame_serialize(const HsysCmdRespFrame_t* frame, uint8_t* buffer, 
                         uint32_t bufferSize, uint32_t* frameSize) 
{
    if (!frame || !buffer) return false;
    
    uint32_t requiredSize = HSYS_MIN_FRAME_SIZE + frame->dataSize;
    
    if (bufferSize < requiredSize) {
        return false;
    }
    
    uint32_t offset = 0;
    
    // STX - 4 bytes
    memcpy(buffer + offset, frame->startCode, 4);
    offset += 4;
    
    // VER - 1 byte
    buffer[offset++] = frame->version;

    // FLAGS - 1 byte
    buffer[offset++] = frame->flags;

    // TYPE - 1 byte
    buffer[offset++] = frame->type;
    
    // CMDID - 2 bytes (little endian)
    buffer[offset++] = (uint8_t)(frame->commandId & 0xFF);
    buffer[offset++] = (uint8_t)((frame->commandId >> 8) & 0xFF);
    
    // RSP-PIPE-ID - 6 bytes
    memcpy(buffer + offset, frame->responsePipeId, HSYS_RSP_PIPE_ID_SIZE);
    offset += HSYS_RSP_PIPE_ID_SIZE;
    
    // SEQ NU - 1 byte
    buffer[offset++] = frame->sequenceNumber;
    
    // SIZE - 4 bytes (little endian)
    buffer[offset++] = (uint8_t)(frame->dataSize & 0xFF);
    buffer[offset++] = (uint8_t)((frame->dataSize >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)((frame->dataSize >> 16) & 0xFF);
    buffer[offset++] = (uint8_t)((frame->dataSize >> 24) & 0xFF);
    
    // DATA - variable size
    if (frame->dataSize > 0) 
    {
        memcpy(buffer + offset, frame->data, frame->dataSize);
        offset += frame->dataSize;
    }
    
    // Calculate CRC32 for the frame (excluding CRC32 field itself)
    uint32_t crc32 = hsys_calculate_crc32(buffer, offset);
    
    // CRC32 - 4 bytes (little endian)
    buffer[offset++] = (uint8_t)(crc32 & 0xFF);
    buffer[offset++] = (uint8_t)((crc32 >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)((crc32 >> 16) & 0xFF);
    buffer[offset++] = (uint8_t)((crc32 >> 24) & 0xFF);
    
    if (frameSize) {
        *frameSize = offset;
    }
    
    return true;
}

// Deserialize buffer to frame
bool hsys_frame_deserialize(HsysCmdRespFrame_t* frame, const uint8_t* buffer, uint32_t bufferSize) 
{    
    if (!frame || !buffer || bufferSize < HSYS_MIN_FRAME_SIZE) 
    {
        LOG_MSG_ERROR(LOG_EN, "Invalid frame or buffer");
        return false;
    }
    
    uint32_t offset = 0;
    
    // STX - 4 bytes
    memcpy(frame->startCode, buffer + offset, 4);
    offset += 4;
    
    // Verify STX
    if (strncmp(frame->startCode, HSYS_STX, 4) != 0) 
    {
        LOG_MSG_DEBUG(LOG_EN, "Invalid start code");
        return false;
    }
    
    // VER - 1 byte
    frame->version = buffer[offset++];

    // FLAGS - 1 byte
    frame->flags = buffer[offset++];

    // TYPE - 1 byte
    frame->type = buffer[offset++];
    
    // CMDID - 2 bytes (little endian)
    frame->commandId = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;
    
    // RSP-PIPE-ID - 6 bytes
    memcpy(frame->responsePipeId, buffer + offset, HSYS_RSP_PIPE_ID_SIZE);
    offset += HSYS_RSP_PIPE_ID_SIZE;
    
    // SEQ NU - 1 byte
    frame->sequenceNumber = buffer[offset++];
    
    // SIZE - 4 bytes (little endian)
    frame->dataSize = buffer[offset] | (buffer[offset + 1] << 8) | 
                     (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
    offset += 4;
    
    // Verify data size
    if (frame->dataSize > HSYS_MAX_DATA_SIZE || 
        bufferSize < (HSYS_MIN_FRAME_SIZE + frame->dataSize)) 
    {
        LOG_MSG_DEBUG(LOG_EN, "Invalid buffer size or data size %d", frame->dataSize);
        return false;
    }
    
    // DATA - variable size
    if (frame->dataSize > 0) 
    {
        LOG_MSG_DEBUG(LOG_EN, "Message processed successfully %d", frame->dataSize);
        // if (!allocate_data(frame, frame->dataSize)) {
        //     return false;
        // }
        memcpy(frame->data, buffer + offset, frame->dataSize);
        offset += frame->dataSize;
    }
    
    // CRC32 - 4 bytes (little endian)
    frame->crc32 = buffer[offset] | (buffer[offset + 1] << 8) | 
                  (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
    
    // Verify CRC32
    uint32_t calculatedCrc = hsys_calculate_crc32(buffer, offset);
    if (calculatedCrc != frame->crc32) 
    {
        LOG_MSG_DEBUG(LOG_EN, "Invalid CRC32 (0x%08X != 0x%08X)", calculatedCrc, frame->crc32);
        return false;
    }
    
    return true;
}

// Check if frame is valid
bool hsys_frame_is_valid(const HsysCmdRespFrame_t* frame) 
{
    if (!frame) return false;
    
    return (strncmp(frame->startCode, HSYS_STX, 4) == 0) &&
           (frame->version == HSYS_PROTOCOL_VERSION) &&
           (frame->dataSize <= HSYS_MAX_DATA_SIZE);
}

// Clear frame data
void hsys_frame_clear(HsysCmdRespFrame_t* frame) 
{
    if (!frame) return;
    
    // data[] is an embedded array (not heap-allocated), just memset the whole struct
    memset(frame, 0, sizeof(HsysCmdRespFrame_t));
    strncpy(frame->startCode, HSYS_STX, 4);
    frame->version = HSYS_PROTOCOL_VERSION;
}

// Free frame resources
// void hsys_frame_free(HsysCmdRespFrame_t* frame) {
//     if (!frame) return;
    
//     if (frame->dataAllocated && frame->data) {
//         free(frame->data);
//         frame->data = NULL;
//         frame->dataAllocated = false;
//     }
//     frame->dataSize = 0;
// }

// Getter functions
HsysCommandId_t hsys_frame_get_command_id(const HsysCmdRespFrame_t* frame) 
{
    return frame ? (HsysCommandId_t)frame->commandId : (HsysCommandId_t)0;
}

uint8_t hsys_frame_get_flags(const HsysCmdRespFrame_t* frame)
{
    return frame ? frame->flags : 0;
}

uint8_t hsys_frame_get_type(const HsysCmdRespFrame_t* frame)
{
    return frame ? frame->type : 0;
}

void hsys_frame_get_response_pipe_id(const HsysCmdRespFrame_t* frame, char * response_pipe_id) 
{
    memset(response_pipe_id, 0, HSYS_RSP_PIPE_ID_SIZE + 1);
    if (frame) 
    {
        memcpy(response_pipe_id, frame->responsePipeId, HSYS_RSP_PIPE_ID_SIZE);
    }
}

uint8_t hsys_frame_get_sequence_number(const HsysCmdRespFrame_t* frame) 
{
    return frame ? frame->sequenceNumber : 0;
}

const uint8_t* hsys_frame_get_data(const HsysCmdRespFrame_t* frame) 
{
    return frame ? frame->data : NULL;
}

uint32_t hsys_frame_get_data_size(const HsysCmdRespFrame_t* frame) 
{
    return frame ? frame->dataSize : 0;
}

uint32_t hsys_frame_get_crc32(const HsysCmdRespFrame_t* frame) 
{
    return frame ? frame->crc32 : 0;
}

// Calculate CRC32 using external crc32 implementation
uint32_t hsys_calculate_crc32(const uint8_t* data, uint32_t length) 
{
    // Initialize CRC with 0xFFFFFFFF and finalize with XOR 0xFFFFFFFF
    uint32_t crc = 0xFFFFFFFF;
    crc = crc32_update(crc, data, length);
    return crc ^ 0xFFFFFFFF;
}

// Print frame for debugging (requires LOG_MSG_DEBUG or similar)
void hsys_print_frame(const HsysCmdRespFrame_t* frame) 
{
    if (!frame) return;
    
    LOG_MSG_DEBUG(LOG_EN, "=== HSYS Frame ===");
    LOG_DEBUG_BUFFER(LOG_EN, "STX: ", frame->startCode, 4);
    
    LOG_MSG_DEBUG(LOG_EN, "Flags: 0x%02X", frame->flags);
    LOG_MSG_DEBUG(LOG_EN, "Type: 0x%02X", frame->type);
    LOG_MSG_DEBUG(LOG_EN, "Version: 0x%02X", frame->version);
    LOG_MSG_DEBUG(LOG_EN, "Command ID: 0x%04X", frame->commandId);

    LOG_DEBUG_BUFFER(LOG_EN, "Response Pipe ID: ", frame->responsePipeId, HSYS_RSP_PIPE_ID_SIZE);

    LOG_MSG_DEBUG(LOG_EN, "Sequence Number: %u", frame->sequenceNumber);
    LOG_MSG_DEBUG(LOG_EN, "Data Size: %u", frame->dataSize);
    LOG_MSG_DEBUG(LOG_EN, "CRC32: 0x%08X", frame->crc32);

    LOG_DEBUG_BUFFER(LOG_EN, "Data: ", frame->data, frame->dataSize);
    LOG_MSG_DEBUG(LOG_EN, "==================");

}

uint32_t hsys_create_basic_response_data(uint8_t* buffer, HsysCmdStatus_t status) 
{
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)status;
    return 1;
}

uint32_t hsys_create_ota_data_response_data(uint8_t* buffer, uint32_t offset_expecting, HsysCmdStatus_t status) 
{
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)status;

    buffer[1] = (uint8_t)((offset_expecting >> 24) & 0xFF);
    buffer[2] = (uint8_t)((offset_expecting >> 16) & 0xFF);
    buffer[3] = (uint8_t)((offset_expecting >> 8) & 0xFF);
    buffer[4] = (uint8_t)(offset_expecting & 0xFF);
    
    return 5;
}

uint32_t hsys_create_ota_status_response_data(uint8_t* buffer, HsysCmdStatus_t cmdStatus, 
                                             HsysOtaStatus_t otaStatus) 
{
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)cmdStatus;
    buffer[1] = (uint8_t)otaStatus;
    return 2;
}

uint32_t hsys_create_fw_version_response_data(uint8_t* buffer, uint32_t fwVersion) 
{
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)((fwVersion >> 24) & 0xFF);
    buffer[1] = (uint8_t)((fwVersion >> 16) & 0xFF);
    buffer[2] = (uint8_t)((fwVersion >> 8) & 0xFF);
    buffer[3] = (uint8_t)(fwVersion & 0xFF);

    return 4;
}

// Command-specific data parsers
bool hsys_parse_ota_start_command_data(const uint8_t* data, uint32_t size, 
                                      HsysOtaId_t* otaId, uint32_t* otaSize) 
{
    if (!data || size < 5 || !otaId || !otaSize) return false;
    
    *otaId = (HsysOtaId_t)data[0];
    *otaSize = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    
    return true;
}

bool hsys_parse_ota_data_command_data(const uint8_t* data, uint32_t size, 
                                     uint32_t* otaOffset, uint8_t* otaDataBuffer, uint16_t* dataSize) 
{
    if (!data || size < 4 || !otaOffset || !otaDataBuffer || !dataSize) return false;

    *otaOffset = data[3] | (data[2] << 8) | (data[1] << 16) | (data[0] << 24);

    uint16_t actualDataSize = size - 4;
    if (actualDataSize > HSYS_OTA_BUFFER_SIZE) return false;
    
    memcpy(otaDataBuffer, data + 4, actualDataSize);
    *dataSize = actualDataSize;
    
    return true;
}

bool hsys_parse_ota_complete_command_data(const uint8_t* data, uint32_t size, uint32_t* otaCrc32) 
{
    if (!data || size < 4 || !otaCrc32) return false;
    
    *otaCrc32 = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    
    return true;
}

bool hsys_parse_basic_response(const uint8_t* data, uint32_t size, HsysCmdStatus_t* status) 
{
    if (!data || size < 1 || !status) return false;
    
    *status = (HsysCmdStatus_t)data[0];
    
    return true;
}

bool hsys_parse_ota_status_response(const uint8_t* data, uint32_t size, 
                                   HsysCmdStatus_t* cmdStatus, HsysOtaStatus_t* otaStatus) 
{
    if (!data || size < 2 || !cmdStatus || !otaStatus) return false;
    
    *cmdStatus = (HsysCmdStatus_t)data[0];
    *otaStatus = (HsysOtaStatus_t)data[1];
    
    return true;
}

bool hsys_parse_fw_version_response(const uint8_t* data, uint32_t size, uint32_t* fwVersion) 
{
    if (!data || size < 4 || !fwVersion) return false;
    
    *fwVersion = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    
    return true;
}
