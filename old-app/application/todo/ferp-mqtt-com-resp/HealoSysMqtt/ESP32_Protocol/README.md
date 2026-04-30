# ESP32 HSYS Protocol Implementation

This folder contains C++ implementation of the HSYS Command/Response Frame protocol specifically designed for ESP32 microcontrollers.

## Files

- **HsysCmdRespFrame.h** - Header file with class definitions, enums, and function prototypes
- **HsysCmdRespFrame.cpp** - Implementation file with all protocol logic
- **example_usage.cpp** - Complete examples showing how to use the protocol

## Dependencies

- **crc32.h** and **crc32.cpp** - External CRC32 implementation (required)
- Arduino.h (for Arduino/ESP32 environment)
- Standard C libraries: stdint.h, string.h

The implementation uses your existing CRC32 functions:
```cpp
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
```

## Protocol Overview

The HSYS protocol frame format:
```
[STX - 4B][VER - 1B][CMDID - 2B][RSP-PIPE-ID - 6B][SEQ NU - 1B][SIZE - 4B][DATA - 1K][CRC32 - 4B]
```

### Frame Fields:
- **STX**: Start code "HSYS" (4 bytes)
- **VER**: Protocol version 0x00 (1 byte)
- **CMDID**: Command identifier (2 bytes)
- **RSP-PIPE-ID**: Response pipe identifier (6 bytes)
- **SEQ NU**: Sequence number (1 byte)
- **SIZE**: Data payload size (4 bytes)
- **DATA**: Variable data payload (up to 1KB)
- **CRC32**: Frame checksum (4 bytes)

## Supported Commands

### OTA Commands:
- `CMD_OTA_START` - Start OTA update process
- `CMD_OTA_DATA` - Send OTA data chunks
- `CMD_OTA_COMPLETE` - Complete OTA process
- `CMD_OTA_GET_STATUS` - Get current OTA status

### System Commands:
- `CMD_GET_FW_VERSION` - Get firmware version
- `CMD_GET_FW_VERSION_SUB_1` - Get sub-component firmware version

## Usage

### 1. Include the header file in your ESP32 project:
```cpp
#include "HsysCmdRespFrame.h"
```

### 2. Creating a command frame:
```cpp
// Create frame for OTA start command
HsysCmdRespFrame frame(CMD_OTA_START, "PIPE01", 1);

// Create command data
uint8_t cmdData[5];
uint32_t dataSize = HsysCmdRespFrame::createOtaStartCommandData(
    cmdData, OTAMAIN, 1024000);  // 1MB firmware

// Set data in frame
frame.setData(cmdData, dataSize);

// Serialize for transmission
uint8_t buffer[1050];
uint32_t frameSize;
if (frame.serialize(buffer, sizeof(buffer), &frameSize)) {
    // Send buffer via MQTT, Serial, etc.
}
```

### 3. Parsing received frames:
```cpp
HsysCmdRespFrame receivedFrame;
if (receivedFrame.deserialize(buffer, bufferSize)) {
    // Successfully parsed frame
    HsysCommandId_t cmdId = receivedFrame.getCommandId();
    const uint8_t* data = receivedFrame.getData();
    uint32_t dataSize = receivedFrame.getDataSize();
    
    // Process based on command ID
    switch (cmdId) {
        case CMD_OTA_START:
            // Handle OTA start
            break;
        // ... other commands
    }
}
```

### 4. Creating responses:
```cpp
// Create response frame
HsysCmdRespFrame response(CMD_OTA_START, "RESP01", 1);

// Create response data
uint8_t respData[1];
uint32_t respSize = HsysCmdRespFrame::createBasicResponseData(
    respData, CMD_STATUS_OK);

response.setData(respData, respSize);

// Serialize and send response
uint8_t responseBuffer[1050];
uint32_t responseSize;
if (response.serialize(responseBuffer, sizeof(responseBuffer), &responseSize)) {
    // Send response
}
```

## OTA Implementation Example

The protocol includes complete OTA (Over-The-Air) update support:

1. **Start OTA**: Host sends `CMD_OTA_START` with target ID and size
2. **Send Data**: Host sends multiple `CMD_OTA_DATA` commands with chunks
3. **Complete**: Host sends `CMD_OTA_COMPLETE` with CRC32 for verification
4. **Status**: Host can query status with `CMD_OTA_GET_STATUS`

## Memory Considerations

- Maximum frame size: ~1050 bytes (22 bytes header + 1024 bytes data + 4 bytes CRC)
- Dynamic memory allocation for data payload
- Automatic memory management in destructor
- Uses external CRC32 implementation (no built-in lookup table)

## Arduino IDE Integration

1. Copy `HsysCmdRespFrame.h`, `HsysCmdRespFrame.cpp`, `crc32.h`, and `crc32.cpp` to your Arduino sketch folder
2. Include the header in your main sketch file
3. Use the provided examples as reference

**Required Files:**
- HsysCmdRespFrame.h
- HsysCmdRespFrame.cpp  
- crc32.h (your existing CRC32 header)
- crc32.cpp (your existing CRC32 implementation)

## Error Handling

The implementation includes comprehensive error checking:
- Frame size validation
- CRC32 verification
- Command parameter validation
- Memory allocation checks

## Debugging

Use the built-in debug function to print frame details:
```cpp
HsysCmdRespFrame::printFrame(frame);
```

This will output frame contents to Serial for debugging.

## Dependencies

- **crc32.h** and **crc32.cpp** - External CRC32 implementation (required)
- Arduino.h (for Arduino/ESP32 environment)  
- Standard C libraries: stdint.h, string.h

The implementation uses your existing CRC32 functions:
```cpp
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
```

## Compatible Platforms

- ESP32
- ESP8266 (with minor modifications)
- Arduino boards with sufficient memory
- Any microcontroller with C++ support

## Thread Safety

The current implementation is not thread-safe. If using in multi-threaded environment (FreeRTOS), implement appropriate locking mechanisms.

## Future Enhancements

- Add support for fragmented frames (>1KB data)
- Implement acknowledgment mechanism
- Add encryption support
- Optimize memory usage for smaller microcontrollers
