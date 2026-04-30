/*
 * ESP32 HsysCmdRespFrame C Functions Usage Example
 * 
 * This file demonstrates how to use the HsysCmdRespFrame C functions
 * for ESP32 applications with the HSYS protocol.
 * 
 * Dependencies:
 * - HsysCmdRespFrame.h/.cpp (C function implementation)
 * - crc32.h/.cpp (external CRC32 implementation)
 */

#include "HsysCmdRespFrame.h"
#include <stdio.h>

// Example: Creating and sending an OTA Start command
void sendOtaStartCommand() {
    // Create frame
    HsysCmdRespFrame_t frame;
    if (!hsys_frame_init_with_params(&frame, HSYS_CMD_OTA_START, "PIPE01", 1)) {
        printf("Failed to initialize frame\n");
        return;
    }
    
    // Create OTA start command data
    uint8_t cmdData[5];
    uint32_t dataSize = hsys_create_ota_start_command_data(
        cmdData, HSYS_OTA_FW, 1024000);  // 1MB firmware
    
    // Set data in frame
    if (!hsys_frame_set_data(&frame, cmdData, dataSize)) {
        printf("Failed to set frame data\n");
        hsys_frame_free(&frame);
        return;
    }
    
    // Serialize frame for transmission
    uint8_t buffer[1050];  // Max frame size
    uint32_t frameSize;
    
    if (hsys_frame_serialize(&frame, buffer, sizeof(buffer), &frameSize)) {
        printf("OTA Start command created successfully\n");
        printf("Frame size: %u\n", frameSize);
        
        // Send buffer via MQTT, Serial, or other communication method
        // Example: mqttClient.publish("command/topic", buffer, frameSize);
        
        // Print frame for debugging
        hsys_print_frame(&frame);
    }
    
    hsys_frame_free(&frame);
}

// Example: Complete OTA workflow using C functions
typedef struct {
    uint32_t expectedOtaSize;
    uint32_t receivedBytes;
    uint32_t expectedCrc32;
    HsysOtaStatus_t currentStatus;
} ESP32OtaHandler_t;

// Initialize OTA handler
void ota_handler_init(ESP32OtaHandler_t* handler) {
    if (!handler) return;
    
    handler->expectedOtaSize = 0;
    handler->receivedBytes = 0;
    handler->expectedCrc32 = 0;
    handler->currentStatus = HSYS_OTA_IDLE;
}

// Handle incoming OTA start command
bool ota_handler_start(ESP32OtaHandler_t* handler, const uint8_t* data, uint32_t size) {
    if (!handler) return false;
    
    HsysOtaId_t otaId;
    uint32_t otaSize;
    
    if (hsys_parse_ota_start_command_data(data, size, &otaId, &otaSize)) {
        handler->expectedOtaSize = otaSize;
        handler->receivedBytes = 0;
        handler->currentStatus = HSYS_OTA_IN_PROGRESS;
        
        printf("Starting OTA for target: %d, Size: %u\n", (int)otaId, otaSize);
        return true;
    }
    return false;
}

// Main message handler for ESP32
void handleIncomingMessage(const uint8_t* buffer, uint32_t bufferSize) {
    static ESP32OtaHandler_t otaHandler = {0};
    static bool otaHandlerInitialized = false;
    
    if (!otaHandlerInitialized) {
        ota_handler_init(&otaHandler);
        otaHandlerInitialized = true;
    }
    
    HsysCmdRespFrame_t frame;
    if (!hsys_frame_init(&frame)) {
        printf("Failed to initialize frame\n");
        return;
    }
    
    if (hsys_frame_deserialize(&frame, buffer, bufferSize)) {
        printf("Message processed successfully\n");
        hsys_print_frame(&frame);
    } else {
        printf("Failed to parse incoming message\n");
    }
    
    hsys_frame_free(&frame);
}

// Setup function (called once)
void setup() {
    printf("ESP32 HSYS Protocol C Functions Example\n");
    printf("======================================\n");
    
    // Example: Send OTA start command
    sendOtaStartCommand();
}

// Loop function (called repeatedly)
void loop() {
    // For Arduino environment, use delay
    #ifdef ARDUINO
    delay(1000);
    #endif
}

// Main function for non-Arduino environments
#ifndef ARDUINO
int main() {
    setup();
    return 0;
}
#endif
