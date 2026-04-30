#include "HsysCmdRespFrame.h"

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
static bool allocate_data(HsysCmdRespFrame_t* frame, uint32_t size) {
    // Free existing data first
    if (frame->dataAllocated && frame->data) {
        free(frame->data);
        frame->data = NULL;
        frame->dataAllocated = false;
    }
    
    if (size == 0) {
        frame->data = NULL;
        frame->dataSize = 0;
        return true;
    }
    
    frame->data = (uint8_t*)malloc(size);
    if (!frame->data) {
        return false;
    }
    
    frame->dataAllocated = true;
    return true;
}

// Initialize frame with default values
bool hsys_frame_init(HsysCmdRespFrame_t* frame) {
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
                                const char* responsePipeId, uint8_t sequenceNumber) {
    if (!frame) return false;
    
    if (!hsys_frame_init(frame)) {
        return false;
    }
    
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
    
    if (!allocate_data(frame, size)) {
        return false;
    }
    
    if (data && size > 0) {
        memcpy(frame->data, data, size);
    }
    
    frame->dataSize = size;
    return true;
}

// Serialize frame to buffer
bool hsys_frame_serialize(const HsysCmdRespFrame_t* frame, uint8_t* buffer, 
                         uint32_t bufferSize, uint32_t* frameSize) {
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
    if (frame->dataSize > 0 && frame->data) {
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
bool hsys_frame_deserialize(HsysCmdRespFrame_t* frame, const uint8_t* buffer, uint32_t bufferSize) {
    if (!frame || !buffer || bufferSize < HSYS_MIN_FRAME_SIZE) {
        return false;
    }
    
    uint32_t offset = 0;
    
    // STX - 4 bytes
    memcpy(frame->startCode, buffer + offset, 4);
    offset += 4;
    
    // Verify STX
    if (strncmp(frame->startCode, HSYS_STX, 4) != 0) {
        return false;
    }
    
    // VER - 1 byte
    frame->version = buffer[offset++];
    
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
        bufferSize < (HSYS_MIN_FRAME_SIZE + frame->dataSize)) {
        return false;
    }
    
    // DATA - variable size
    if (frame->dataSize > 0) {
        if (!allocate_data(frame, frame->dataSize)) {
            return false;
        }
        memcpy(frame->data, buffer + offset, frame->dataSize);
        offset += frame->dataSize;
    }
    
    // CRC32 - 4 bytes (little endian)
    frame->crc32 = buffer[offset] | (buffer[offset + 1] << 8) | 
                  (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
    
    // Verify CRC32
    uint32_t calculatedCrc = hsys_calculate_crc32(buffer, offset);
    if (calculatedCrc != frame->crc32) {
        return false;
    }
    
    return true;
}

// Check if frame is valid
bool hsys_frame_is_valid(const HsysCmdRespFrame_t* frame) {
    if (!frame) return false;
    
    return (strncmp(frame->startCode, HSYS_STX, 4) == 0) &&
           (frame->version == HSYS_PROTOCOL_VERSION) &&
           (frame->dataSize <= HSYS_MAX_DATA_SIZE);
}

// Clear frame data
void hsys_frame_clear(HsysCmdRespFrame_t* frame) {
    if (!frame) return;
    
    if (frame->dataAllocated && frame->data) {
        free(frame->data);
    }
    
    memset(frame, 0, sizeof(HsysCmdRespFrame_t));
    strncpy(frame->startCode, HSYS_STX, 4);
    frame->version = HSYS_PROTOCOL_VERSION;
}

// Free frame resources
void hsys_frame_free(HsysCmdRespFrame_t* frame) {
    if (!frame) return;
    
    if (frame->dataAllocated && frame->data) {
        free(frame->data);
        frame->data = NULL;
        frame->dataAllocated = false;
    }
    frame->dataSize = 0;
}

// Getter functions
HsysCommandId_t hsys_frame_get_command_id(const HsysCmdRespFrame_t* frame) {
    return frame ? (HsysCommandId_t)frame->commandId : 0;
}

const char* hsys_frame_get_response_pipe_id(const HsysCmdRespFrame_t* frame) {
    return frame ? frame->responsePipeId : NULL;
}

uint8_t hsys_frame_get_sequence_number(const HsysCmdRespFrame_t* frame) {
    return frame ? frame->sequenceNumber : 0;
}

const uint8_t* hsys_frame_get_data(const HsysCmdRespFrame_t* frame) {
    return frame ? frame->data : NULL;
}

uint32_t hsys_frame_get_data_size(const HsysCmdRespFrame_t* frame) {
    return frame ? frame->dataSize : 0;
}

uint32_t hsys_frame_get_crc32(const HsysCmdRespFrame_t* frame) {
    return frame ? frame->crc32 : 0;
}

// Calculate CRC32 using external crc32 implementation
uint32_t hsys_calculate_crc32(const uint8_t* data, uint32_t length) {
    // Initialize CRC with 0xFFFFFFFF and finalize with XOR 0xFFFFFFFF
    uint32_t crc = 0xFFFFFFFF;
    crc = crc32_update(crc, data, length);
    return crc ^ 0xFFFFFFFF;
}

// Print frame for debugging (requires printf or similar)
void hsys_print_frame(const HsysCmdRespFrame_t* frame) {
    if (!frame) return;
    
    printf("=== HSYS Frame ===\n");
    printf("STX: ");
    for (int i = 0; i < 4; i++) {
        printf("%c", frame->startCode[i]);
    }
    printf("\n");
    
    printf("Version: 0x%02X\n", frame->version);
    printf("Command ID: 0x%04X\n", frame->commandId);
    
    printf("Response Pipe ID: ");
    for (int i = 0; i < HSYS_RSP_PIPE_ID_SIZE; i++) {
        printf("%c", frame->responsePipeId[i]);
    }
    printf("\n");
    
    printf("Sequence Number: %u\n", frame->sequenceNumber);
    printf("Data Size: %u\n", frame->dataSize);
    printf("CRC32: 0x%08X\n", frame->crc32);
    
    if (frame->dataSize > 0 && frame->data) {
        printf("Data: ");
        for (uint32_t i = 0; i < frame->dataSize && i < 32; i++) {
            printf("0x%02X ", frame->data[i]);
        }
        if (frame->dataSize > 32) {
            printf("...");
        }
        printf("\n");
    }
    printf("==================\n");
}

// Command-specific data creators
uint32_t hsys_create_ota_start_command_data(uint8_t* buffer, HsysOtaId_t otaId, uint32_t otaSize) {
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)otaId;
    buffer[1] = (uint8_t)(otaSize & 0xFF);
    buffer[2] = (uint8_t)((otaSize >> 8) & 0xFF);
    buffer[3] = (uint8_t)((otaSize >> 16) & 0xFF);
    buffer[4] = (uint8_t)((otaSize >> 24) & 0xFF);
    
    return 5;
}

uint32_t hsys_create_ota_data_command_data(uint8_t* buffer, uint32_t otaOffset, 
                                          const uint8_t* otaDataBuffer, uint16_t dataSize) {
    if (!buffer || !otaDataBuffer || dataSize > HSYS_OTA_BUFFER_SIZE) return 0;
    
    uint32_t offset = 0;
    
    // OTA Offset - 4 bytes
    buffer[offset++] = (uint8_t)(otaOffset & 0xFF);
    buffer[offset++] = (uint8_t)((otaOffset >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)((otaOffset >> 16) & 0xFF);
    buffer[offset++] = (uint8_t)((otaOffset >> 24) & 0xFF);
    
    // OTA Data Buffer
    memcpy(buffer + offset, otaDataBuffer, dataSize);
    offset += dataSize;
    
    return offset;
}

uint32_t hsys_create_ota_complete_command_data(uint8_t* buffer, uint32_t otaCrc32) {
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)(otaCrc32 & 0xFF);
    buffer[1] = (uint8_t)((otaCrc32 >> 8) & 0xFF);
    buffer[2] = (uint8_t)((otaCrc32 >> 16) & 0xFF);
    buffer[3] = (uint8_t)((otaCrc32 >> 24) & 0xFF);
    
    return 4;
}

uint32_t hsys_create_basic_response_data(uint8_t* buffer, HsysCmdStatus_t status) {
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)status;
    return 1;
}

uint32_t hsys_create_ota_status_response_data(uint8_t* buffer, HsysCmdStatus_t cmdStatus, 
                                             HsysOtaStatus_t otaStatus) {
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)cmdStatus;
    buffer[1] = (uint8_t)otaStatus;
    return 2;
}

uint32_t hsys_create_fw_version_response_data(uint8_t* buffer, uint32_t fwVersion) {
    if (!buffer) return 0;
    
    buffer[0] = (uint8_t)(fwVersion & 0xFF);
    buffer[1] = (uint8_t)((fwVersion >> 8) & 0xFF);
    buffer[2] = (uint8_t)((fwVersion >> 16) & 0xFF);
    buffer[3] = (uint8_t)((fwVersion >> 24) & 0xFF);
    
    return 4;
}

// Command-specific data parsers
bool hsys_parse_ota_start_command_data(const uint8_t* data, uint32_t size, 
                                      HsysOtaId_t* otaId, uint32_t* otaSize) {
    if (!data || size < 5 || !otaId || !otaSize) return false;
    
    *otaId = (HsysOtaId_t)data[0];
    *otaSize = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    
    return true;
}

bool hsys_parse_ota_data_command_data(const uint8_t* data, uint32_t size, 
                                     uint32_t* otaOffset, uint8_t* otaDataBuffer, uint16_t* dataSize) {
    if (!data || size < 4 || !otaOffset || !otaDataBuffer || !dataSize) return false;
    
    *otaOffset = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    
    uint16_t actualDataSize = size - 4;
    if (actualDataSize > HSYS_OTA_BUFFER_SIZE) return false;
    
    memcpy(otaDataBuffer, data + 4, actualDataSize);
    *dataSize = actualDataSize;
    
    return true;
}

bool hsys_parse_ota_complete_command_data(const uint8_t* data, uint32_t size, uint32_t* otaCrc32) {
    if (!data || size < 4 || !otaCrc32) return false;
    
    *otaCrc32 = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    
    return true;
}

bool hsys_parse_basic_response(const uint8_t* data, uint32_t size, HsysCmdStatus_t* status) {
    if (!data || size < 1 || !status) return false;
    
    *status = (HsysCmdStatus_t)data[0];
    
    return true;
}

bool hsys_parse_ota_status_response(const uint8_t* data, uint32_t size, 
                                   HsysCmdStatus_t* cmdStatus, HsysOtaStatus_t* otaStatus) {
    if (!data || size < 2 || !cmdStatus || !otaStatus) return false;
    
    *cmdStatus = (HsysCmdStatus_t)data[0];
    *otaStatus = (HsysOtaStatus_t)data[1];
    
    return true;
}

bool hsys_parse_fw_version_response(const uint8_t* data, uint32_t size, uint32_t* fwVersion) {
    if (!data || size < 4 || !fwVersion) return false;
    
    *fwVersion = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    
    return true;
}
