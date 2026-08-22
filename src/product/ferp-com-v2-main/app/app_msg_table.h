// app_msg_table.h
//
// Application message descriptor table.
//
// Descriptors are owned by each typed message class (MsgSensorData,
// MsgTick1000ms, …).  This header assembles them into the flat array
// that hsys_msg_table_init() consumes at startup.
//
// Message IDs are defined in product/app/app_msg_ids.h — never here or in the
// message class files.  This is the only place you manage the full
// descriptor table.
//
// To add a new message:
//   1. Add its ID to product/app/app_msg_ids.h
//   2. Create src/app-messages/msg_<name>.h/.cpp
//   3. #include msg_<name>.h below
//   4. Add MsgXxx::DESCRIPTOR to APP_MSG_TABLE_INIT
//
// Usage in app.cpp:
//   APP_MSG_TABLE_INIT;
//   hsys_msg_table_init(k_msg_table, k_msg_table_count);

#ifndef APP_MSG_TABLE_H
#define APP_MSG_TABLE_H

#include "hsys_msg.h"                   // hsys_msg_desc_t
#include "app_msg_ids.h"                // MSG_ID_* — single source of truth for IDs
#include "msg_tick_1000ms.h"            // MsgTick1000ms
#include "msg_spiffs_ready.h"           // MsgSpiffsReady         (0x0201)
#include "msg_sd_ready.h"               // MsgSdReady             (0x0202)
#include "msg_sd_status.h"              // MsgSdStatus            (0x0203)
#include "msg_time_status.h"            // MsgTimeStatus          (0x0204)
#include "msg_config_ready.h"           // MsgConfigReady         (0x0300)
#include "msg_config_set.h"             // MsgConfigSet           (0x0301)
#include "msg_config_get.h"             // MsgConfigGet           (0x0302)
#include "msg_config_get_wifi.h"        // MsgConfigGetWifi       (0x0303)
#include "msg_config_get_cloud.h"       // MsgConfigGetCloud      (0x0304)
#include "msg_config_get_mqtt.h"        // MsgConfigGetMqtt       (0x0305)
#include "msg_config_get_dt.h"          // MsgConfigGetDT         (0x0306)
#include "msg_config_wifi.h"            // MsgConfigWifi          (0x0307)
#include "msg_config_cloud.h"           // MsgConfigCloud         (0x0308)
#include "msg_config_mqtt.h"            // MsgConfigMqtt          (0x0309)
#include "msg_config_dt.h"              // MsgConfigDT            (0x030A)
#include "msg_config_get_ota.h"         // MsgConfigGetOta        (0x030B)
#include "msg_config_ota.h"             // MsgConfigOta           (0x030C)
#include "msg_config_get_key.h"         // MsgConfigGetKey        (0x030D)
#include "msg_config_value.h"           // MsgConfigValue         (0x030E)
#include "msg_config_updated.h"         // MsgConfigUpdated       (0x030F)
#include "msg_timer_start.h"            // MsgTimerStart          (0x0100)
#include "msg_timer_stop.h"             // MsgTimerStop           (0x0101)
#include "msg_timer_start_response.h"   // MsgTimerStartResponse  (0x0102)
#include "msg_timer_stop_response.h"    // MsgTimerStopResponse   (0x0103)
#include "msg_timer_alarm.h"            // MsgTimerAlarm          (0x0104)
#include "msg_default_btn.h"            // MsgDefaultBtn          (0x0900)
#include "msg_printer_btn.h"            // MsgPrinterBtn          (0x0901)
#include "msg_fuel_pumped.h"            // MsgFuelPumped          (0x0010)
#include "msg_nozzle_state.h"           // MsgNozzleState         (0x0011)
#include "msg_fuel_print_status.h"      // MsgFuelPrintStatus         (0x0012)
#include "msg_totalizer_data.h"          // MsgTotalizerData           (0x0013)
#include "msg_fuel_totalizer.h"         // MsgFuelTotalizer           (0x0014)
#include "msg_wifi_event.h"             // MsgWifiEvent           (0x0A00)
#include "msg_internet_status.h"        // MsgInternetStatus      (0x0A01)
#include "msg_cloud_status.h"           // MsgCloudStatus         (0x0A02)
#include "msg_ota_start_request.h"      // MsgOtaStartRequest     (0x0A03)
#include "msg_ota_start_response.h"     // MsgOtaStartResponse    (0x0A04)
#include "msg_ota_request_driver.h"     // MsgOtaRequestDriver    (0x0A05)
#include "msg_ota_driver_response.h"    // MsgOtaDriverResponse   (0x0A06)
#include "msg_ota_abort_request.h"      // MsgOtaAbortRequest     (0x0A07)
#include "msg_ota_complete_notify.h"    // MsgOtaCompleteNotify   (0x0A08)
#include "msg_ota_event.h"              // MsgOtaEvent            (0x0A09)
#include "msg_ota_progress.h"           // MsgOtaProgress         (0x0A0A)
#include "msg_mqtt_status.h"            // MsgMqttStatus          (0x0A0B)
#include "msg_dev_info_read.h"          // MsgDevInfoRead         (0x0040)
#include "msg_dev_info_write.h"         // MsgDevInfoWrite        (0x0041)
#include "msg_dev_info_value.h"         // MsgDevInfoValue        (0x0042)
#include "msg_system_status.h"          // MsgSystemStatus        (0x0206)
#include "msg_pool_get_json.h"           // MsgPoolGetJson         (0x0207)
#include "msg_pool_json.h"              // MsgPoolJson            (0x0208)
#include "msg_get_file_list_spiffs.h"   // MsgGetFileListSpiffs   (0x0209)
#include "msg_file_list_spiffs.h"       // MsgFileListSpiffs      (0x020A)
#include "msg_system_reboot.h"          // MsgSystemReboot        (0x020B)
#include "msg_sd_cleanup.h"              // MsgSdCleanup           (0x020C)
#include "msg_spiffs_cleanup.h"         // MsgSpiffsCleanup       (0x020D)
#include "msg_http_request.h"           // MsgHttpRequest         (0x005F)
#include "msg_http_result.h"            // MsgHttpResult          (0x005B)
#include "msg_http_response_header.h"   // MsgHttpResponseHeader  (0x005D)

// ---------------------------------------------------------------------------
// Descriptor table macro
// Place APP_MSG_TABLE_INIT; in exactly one .cpp file (app.cpp).
// ---------------------------------------------------------------------------

#define APP_MSG_TABLE_INIT                                                        \
    static const hsys_msg_desc_t k_msg_table[] = {                                \
        MsgTick1000ms::DESCRIPTOR,                                                \
        MsgSpiffsReady::DESCRIPTOR,                                               \
        MsgSdReady::DESCRIPTOR,                                                   \
        MsgSdStatus::DESCRIPTOR,                                                  \
        MsgTimeStatus::DESCRIPTOR,                                                \
        MsgConfigReady::DESCRIPTOR,                                               \
        MsgConfigSet::DESCRIPTOR,                                                 \
        MsgConfigGet::DESCRIPTOR,                                                 \
        MsgConfigGetWifi::DESCRIPTOR,                                             \
        MsgConfigGetCloud::DESCRIPTOR,                                            \
        MsgConfigGetMqtt::DESCRIPTOR,                                             \
        MsgConfigGetDT::DESCRIPTOR,                                               \
        MsgConfigGetOta::DESCRIPTOR,                                              \
        MsgConfigGetKey::DESCRIPTOR,                                              \
        MsgConfigValue::DESCRIPTOR,                                               \
        MsgConfigUpdated::DESCRIPTOR,                                             \
        MsgConfigWifi::DESCRIPTOR,                                                \
        MsgConfigCloud::DESCRIPTOR,                                               \
        MsgConfigMqtt::DESCRIPTOR,                                                \
        MsgConfigDT::DESCRIPTOR,                                                  \
        MsgConfigOta::DESCRIPTOR,                                                 \
        MsgTimerStart::DESCRIPTOR,                                                \
        MsgTimerStop::DESCRIPTOR,                                                 \
        MsgTimerStartResponse::DESCRIPTOR,                                        \
        MsgTimerStopResponse::DESCRIPTOR,                                         \
        MsgTimerAlarm::DESCRIPTOR,                                                \
        MsgDefaultBtn::DESCRIPTOR,                                                \
        MsgPrinterBtn::DESCRIPTOR,                                                \
        MsgFuelPumped::DESCRIPTOR,                                                \
        MsgNozzleState::DESCRIPTOR,                                               \
        MsgFuelPrintStatus::DESCRIPTOR,                                               \
        MsgTotalizerData::DESCRIPTOR,                                                 \
        MsgFuelTotalizer::DESCRIPTOR,                                                 \
        MsgWifiEvent::DESCRIPTOR,                                                 \
        MsgInternetStatus::DESCRIPTOR,                                            \
        MsgCloudStatus::DESCRIPTOR,                                               \
        MsgOtaStartRequest::DESCRIPTOR,                                           \
        MsgOtaStartResponse::DESCRIPTOR,                                          \
        MsgOtaRequestDriver::DESCRIPTOR,                                          \
        MsgOtaDriverResponse::DESCRIPTOR,                                         \
        MsgOtaAbortRequest::DESCRIPTOR,                                           \
        MsgOtaCompleteNotify::DESCRIPTOR,                                         \
        MsgOtaEvent::DESCRIPTOR,                                                  \
        MsgOtaProgress::DESCRIPTOR,                                               \
        MsgMqttStatus::DESCRIPTOR,                                                \
        MsgDevInfoRead::DESCRIPTOR,                                               \
        MsgDevInfoWrite::DESCRIPTOR,                                              \
        MsgDevInfoValue::DESCRIPTOR,                                              \
        MsgSystemStatus::DESCRIPTOR,                                              \
        MsgPoolGetJson::DESCRIPTOR,                                               \
        MsgPoolJson::DESCRIPTOR,                                                  \
        MsgGetFileListSpiffs::DESCRIPTOR,                                         \
        MsgFileListSpiffs::DESCRIPTOR,                                            \
        MsgSystemReboot::DESCRIPTOR,                                              \
        MsgHttpRequest::DESCRIPTOR,                                               \
        MsgHttpResult::DESCRIPTOR,                                                \
        MsgSystemReboot::DESCRIPTOR,                                               \
        MsgSdCleanup::DESCRIPTOR,                                                 \
        MsgSpiffsCleanup::DESCRIPTOR,                                             \
        MsgHttpResponseHeader::DESCRIPTOR,                                        \
    };                                                                            \
    static const uint16_t k_msg_table_count =                                     \
        (uint16_t)(sizeof(k_msg_table) / sizeof(k_msg_table[0]))

#endif // APP_MSG_TABLE_H

