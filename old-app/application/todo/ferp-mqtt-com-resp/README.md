# ferp-mqtt-com-resp — HSYS Frame Protocol v1

This module implements the **HSYS command/response frame protocol** used for MQTT-based communication between the host tool (HealoSysMqtt) and the ESP32 firmware.

---

## Files

| File | Description |
|---|---|
| `ferp_frame_v1.h` | Protocol definitions, structs, enums, and function declarations |
| `ferp_frame_v1.cpp` | Frame serialization, deserialization, and command data helpers |
| `HealoSysMqtt/` | C# Windows host tool that implements the same protocol |

---

## Frame Structure

All frames follow this binary layout (little-endian unless noted):

```
[ STX - 4B ][ VER - 1B ][ FLAGS - 1B ][ TYPE - 1B ][ CMDID - 2B ][ RSP-PIPE-ID - 6B ][ SEQ-NU - 1B ][ SIZE - 4B ][ DATA - up to 2KB ][ CRC32 - 4B ]
```

| Field | Size | Description |
|---|---|---|
| `STX` | 4 B | Start code — always `"HSYS"` (ASCII) |
| `VER` | 1 B | Protocol version — currently `0x00` |
| `FLAGS` | 1 B | Frame flags — application defined |
| `TYPE` | 1 B | Frame type — application defined |
| `CMDID` | 2 B | Command ID (little-endian) — see table below |
| `RSP-PIPE-ID` | 6 B | Response pipe identifier (ASCII, space-padded) |
| `SEQ-NU` | 1 B | Sequence number |
| `SIZE` | 4 B | Data payload size in bytes (little-endian) |
| `DATA` | 0–2048 B | Command or response payload |
| `CRC32` | 4 B | CRC32 over all preceding bytes (little-endian) |

**Minimum frame size (no data):** 24 bytes  
**Maximum data payload:** 2048 bytes

---

## Command IDs

| Name | Value | Direction | Description |
|---|---|---|---|
| `CMD_OTA_START` | `0x0001` | Host → Device | Begin an OTA update session |
| `CMD_OTA_DATA` | `0x0002` | Host → Device | Transfer a chunk of OTA binary data |
| `CMD_OTA_COMPLETE` | `0x0003` | Host → Device | Finalise OTA and verify CRC32 |
| `CMD_OTA_GET_STATUS` | `0x0004` | Host → Device | Query current OTA status |
| `CMD_GET_FW_VERSION` | `0x0005` | Host → Device | Request main firmware version |
| `CMD_GET_FW_VERSION_SUB_1` | `0x0006` | Host → Device | Request sub-firmware version (e.g. ESP07) |

---

## OTA Target IDs

| Name | Value | Description |
|---|---|---|
| `OTAMAIN` | `0x00` | Main ESP32 firmware |
| `OTASUB_1` | `0x01` | Sub-firmware (e.g. ESP07) |

---

## Status Codes

### Command Status (`HsysCmdStatus_t`)

| Name | Value | Description |
|---|---|---|
| `CMD_STATUS_OK` | `0x00` | Success |
| `CMD_STATUS_ERROR` | `0x01` | Generic error |
| `CMD_STATUS_INVALID_COMMAND` | `0x02` | Unrecognised command ID |
| `CMD_STATUS_INVALID_DATA` | `0x03` | Payload data is invalid |
| `CMD_STATUS_CRC_ERROR` | `0x04` | CRC32 mismatch |
| `CMD_STATUS_OTA_ERROR` | `0x05` | OTA-specific error |
| `CMD_STATUS_BUSY` | `0x06` | Device is busy |

### OTA Status (`HsysOtaStatus_t`)

| Name | Value | Description |
|---|---|---|
| `OTA_IDLE` | `0x00` | No OTA in progress |
| `OTA_IN_PROGRESS` | `0x01` | OTA transfer ongoing |
| `OTA_COMPLETED` | `0x02` | OTA finished successfully |

---

## Command Payload Formats

### `CMD_OTA_START`
**Command data:**
```
[ OTAID - 1B ][ OTASIZE - 4B (little-endian) ]
```

**Response data:**
```
[ CMDSTATUS - 1B ]
```

---

### `CMD_OTA_DATA`
**Command data:**
```
[ OTAOFFSET - 4B (big-endian) ][ OTADATABUFFER - up to 2048B ]
```

**Response data:**
```
[ CMDSTATUS - 1B ][ EXPECTED_OFFSET - 4B (big-endian) ]
```

---

### `CMD_OTA_COMPLETE`
**Command data:**
```
[ OTACRC32 - 4B (little-endian) ]
```

**Response data:**
```
[ CMDSTATUS - 1B ]
```

---

### `CMD_OTA_GET_STATUS`
**Command data:** *(none)*

**Response data:**
```
[ CMDSTATUS - 1B ][ OTASTATUS - 1B ]
```

---

### `CMD_GET_FW_VERSION` / `CMD_GET_FW_VERSION_SUB_1`
**Command data:** *(none)*

**Response data:**
```
[ FWVERSION - 4B (big-endian) ]
```

---

## API Reference

### Frame Lifecycle

```c
// Zero-initialise a frame with default STX and version
bool hsys_frame_init(HsysCmdRespFrame_t* frame);

// Initialise with all header parameters
bool hsys_frame_init_with_params(HsysCmdRespFrame_t* frame, HsysCommandId_t commandId,
                                 uint8_t flags, uint8_t type,
                                 const char* responsePipeId, uint8_t sequenceNumber);

// Attach a data payload to the frame
bool hsys_frame_set_data(HsysCmdRespFrame_t* frame, const uint8_t* data, uint32_t size);

// Serialise frame to a byte buffer (computes CRC32 automatically)
bool hsys_frame_serialize(const HsysCmdRespFrame_t* frame, uint8_t* buffer,
                          uint32_t bufferSize, uint32_t* frameSize);

// Deserialise a byte buffer into a frame (verifies STX and CRC32)
bool hsys_frame_deserialize(HsysCmdRespFrame_t* frame, const uint8_t* buffer, uint32_t bufferSize);

// Validate STX, version, and data size
bool hsys_frame_is_valid(const HsysCmdRespFrame_t* frame);

// Reset frame to defaults
void hsys_frame_clear(HsysCmdRespFrame_t* frame);

// Print frame fields to debug log
void hsys_print_frame(const HsysCmdRespFrame_t* frame);
```

### Getters

```c
HsysCommandId_t  hsys_frame_get_command_id(const HsysCmdRespFrame_t* frame);
uint8_t          hsys_frame_get_flags(const HsysCmdRespFrame_t* frame);
uint8_t          hsys_frame_get_type(const HsysCmdRespFrame_t* frame);
void             hsys_frame_get_response_pipe_id(const HsysCmdRespFrame_t* frame, char* out);
uint8_t          hsys_frame_get_sequence_number(const HsysCmdRespFrame_t* frame);
const uint8_t*   hsys_frame_get_data(const HsysCmdRespFrame_t* frame);
uint32_t         hsys_frame_get_data_size(const HsysCmdRespFrame_t* frame);
uint32_t         hsys_frame_get_crc32(const HsysCmdRespFrame_t* frame);
```

---

## CRC32

CRC32 is computed over **all bytes preceding the CRC32 field** (i.e. the full frame excluding the last 4 bytes).  
The algorithm uses the standard IEEE 802.3 polynomial (`0xEDB88320`, reflected) with an initial value of `0xFFFFFFFF` and a final XOR of `0xFFFFFFFF`.

---

## Usage Example (C++)

```cpp
#include "ferp_frame_v1.h"

// Build and serialise an OTA start command
HsysCmdRespFrame_t frame;
uint8_t txBuffer[HSYS_MIN_FRAME_SIZE + 5];
uint32_t frameSize = 0;

hsys_frame_init_with_params(&frame, CMD_OTA_START, 0x00, 0x00, "PIPE01", 1);

uint8_t dataBuffer[5];
uint32_t dataSize = hsys_create_ota_start_command_data(dataBuffer, OTAMAIN, firmwareSize);
hsys_frame_set_data(&frame, dataBuffer, dataSize);

hsys_frame_serialize(&frame, txBuffer, sizeof(txBuffer), &frameSize);

// Deserialise an incoming response
HsysCmdRespFrame_t rxFrame;
if (hsys_frame_deserialize(&rxFrame, rxBuffer, rxLength)) {
    HsysCmdStatus_t status;
    hsys_parse_basic_response(rxFrame.data, rxFrame.dataSize, &status);
}
```

---

## Host Tool

The `HealoSysMqtt/` subfolder contains a C# (.NET 8) Windows Forms application that implements the same protocol and communicates with the device over MQTT.  
See `HealoSysMqtt/HealoSysMqtt/Libs/Mqtt/HsysCmdRespFrame.cs` for the equivalent C# implementation.
