/*
 * app.cpp — FERP receipt printer application wiring.
 *
 * Shared by ferp-printer-com-v1 (UART printer, ESP32) and ferp-printer-usb-v1
 * (USB printer, ESP32-S3); the only differences live in app_hw_config.h
 * (transport, pins, OTA target name).  Same structure as the ferp-com
 * products: config table, device info, codec / MQTT / web route tables,
 * OTA tables, pool, module and task tables, then app_init().
 *
 * Modules: Ticker, Sysmon, Spiffs, Config, Timer, Wifi, Internet, DeviceInfo,
 *          OTA (+ MQTT / web / cloud-poll sources), WebServer (port 80, with
 *          the legacy printer API), Mqtt, Http, UdpLog,
 *          Printer, PrinterApi, CloudPrint.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "pal_system.h"
#include "pal_logger.h"

/* HSYS architecture */
#include "hsys_pool.h"
#include "hsys_module.h"
#include "hsys_msg.h"
#include "hsys_task_mgr.h"

/* Application modules */
#include "ticker.h"
#include "module_sysmon.h"
#include "module_spiffs.h"
#include "module_config.h"
#include "module_timer.h"
#include "module_internet.h"
#include "module_wifi.h"
#include "module_ota.h"
#include "ModuleWebClientOta.h"
#include "ModuleWebServer.h"
#include "ModuleMqtt.h"
#include "module_device_info.h"
#include "module_http.h"
#include "ModuleUdpLog.h"
#include "ModulePrinter.h"
#include "ModulePrinterApi.h"
#include "ModuleCloudPrint.h"
#include "app_rootca.h"
#include "app_hw_config.h"

#if defined(PRINTER_TRANSPORT_USB)
#include "printer_transport_usb.h"
#else
#include "printer_transport_uart.h"
#endif

#include "ota_driver_esp32_main.h"

#include "app_msg_table.h"
#include "app_config.h"
#include "app_device_info.h"
#include "app_spiffs.h"
#include "hsys_config.h"
#include "hsys_type.h"
#include "hsys_task.h"

#include "version.h"

// Codec registry
#include "pal_crash_log.h"
#include "app_msg_codec.h"
#include "msg_config_get_mqtt.h"
#include "msg_config_get_wifi.h"
#include "msg_config_get_cloud.h"
#include "msg_config_get_ota.h"
#include "msg_config_get_key.h"
#include "msg_config_set.h"
#include "msg_config_value.h"
#include "msg_config_mqtt.h"
#include "msg_config_wifi.h"
#include "msg_internet_status.h"
#include "msg_ota_event.h"
#include "msg_ota_progress.h"
#include "msg_config_cloud.h"
#include "msg_config_ota.h"
#include "msg_mqtt_status.h"
#include "msg_system_reboot.h"
#include "msg_spiffs_ready.h"
#include "msg_spiffs_cleanup.h"
#include "msg_wifi_event.h"
#include "msg_config_ready.h"
#include "msg_config_get.h"
#include "msg_ota_start_request.h"
#include "msg_ota_abort_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_request_driver.h"
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
#include "msg_timer_start_response.h"
#include "msg_timer_stop_response.h"
#include "msg_timer_alarm.h"
#include "msg_tick_1000ms.h"
#include "msg_dev_info_read.h"
#include "msg_dev_info_value.h"
#include "msg_pool_get_json.h"
#include "msg_pool_json.h"
#include "msg_get_file_list_spiffs.h"
#include "msg_file_list_spiffs.h"
#include "msg_print_job.h"
#include "msg_print_result.h"
#include "msg_printer_cmd.h"
#include "msg_printer_get_status.h"
#include "msg_printer_status.h"
#include "msg_cloud_print_get_status.h"
#include "msg_cloud_print_status.h"

#define __TAG__    "APP     "

// ============================================================================
// Root CA — Google Trust Services GTS Root R1 (cloud + OTA endpoints)
// ============================================================================

const char* const global_root_ca = \
"-----BEGIN CERTIFICATE-----\n"
"MIIFYjCCBEqgAwIBAgIQd70NbNs2+RrqIQ/E8FjTDTANBgkqhkiG9w0BAQsFADBX\n"
"MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n"
"CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIwMDYx\n"
"OTAwMDA0MloXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n"
"GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFIx\n"
"MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAthECix7joXebO9y/lD63\n"
"ladAPKH9gvl9MgaCcfb2jH/76Nu8ai6Xl6OMS/kr9rH5zoQdsfnFl97vufKj6bwS\n"
"iV6nqlKr+CMny6SxnGPb15l+8Ape62im9MZaRw1NEDPjTrETo8gYbEvs/AmQ351k\n"
"KSUjB6G00j0uYODP0gmHu81I8E3CwnqIiru6z1kZ1q+PsAewnjHxgsHA3y6mbWwZ\n"
"DrXYfiYaRQM9sHmklCitD38m5agI/pboPGiUU+6DOogrFZYJsuB6jC511pzrp1Zk\n"
"j5ZPaK49l8KEj8C8QMALXL32h7M1bKwYUH+E4EzNktMg6TO8UpmvMrUpsyUqtEj5\n"
"cuHKZPfmghCN6J3Cioj6OGaK/GP5Afl4/Xtcd/p2h/rs37EOeZVXtL0m79YB0esW\n"
"CruOC7XFxYpVq9Os6pFLKcwZpDIlTirxZUTQAs6qzkm06p98g7BAe+dDq6dso499\n"
"iYH6TKX/1Y7DzkvgtdizjkXPdsDtQCv9Uw+wp9U7DbGKogPeMa3Md+pvez7W35Ei\n"
"Eua++tgy/BBjFFFy3l3WFpO9KWgz7zpm7AeKJt8T11dleCfeXkkUAKIAf5qoIbap\n"
"sZWwpbkNFhHax2xIPEDgfg1azVY80ZcFuctL7TlLnMQ/0lUTbiSw1nH69MG6zO0b\n"
"9f6BQdgAmD06yK56mDcYBZUCAwEAAaOCATgwggE0MA4GA1UdDwEB/wQEAwIBhjAP\n"
"BgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBTkrysmcRorSCeFL1JmLO/wiRNxPjAf\n"
"BgNVHSMEGDAWgBRge2YaRQ2XyolQL30EzTSo//z9SzBgBggrBgEFBQcBAQRUMFIw\n"
"JQYIKwYBBQUHMAGGGWh0dHA6Ly9vY3NwLnBraS5nb29nL2dzcjEwKQYIKwYBBQUH\n"
"MAKGHWh0dHA6Ly9wa2kuZ29vZy9nc3IxL2dzcjEuY3J0MDIGA1UdHwQrMCkwJ6Al\n"
"oCOGIWh0dHA6Ly9jcmwucGtpLmdvb2cvZ3NyMS9nc3IxLmNybDA7BgNVHSAENDAy\n"
"MAgGBmeBDAECATAIBgZngQwBAgIwDQYLKwYBBAHWeQIFAwIwDQYLKwYBBAHWeQIF\n"
"AwMwDQYJKoZIhvcNAQELBQADggEBADSkHrEoo9C0dhemMXoh6dFSPsjbdBZBiLg9\n"
"NR3t5P+T4Vxfq7vqfM/b5A3Ri1fyJm9bvhdGaJQ3b2t6yMAYN/olUazsaL+yyEn9\n"
"WprKASOshIArAoyZl+tJaox118fessmXn1hIVw41oeQa1v1vg4Fv74zPl6/AhSrw\n"
"9U5pCZEt4Wi4wStz6dTZ/CLANx8LZh1J7QJVj2fhMtfTJr9w4z30Z209fOU0iOMy\n"
"+qduBmpvvYuR7hZL6Dupszfnw0Skfths18dG9ZKb59UhvmaSGZRVbNQpsg3BZlvi\n"
"d0lIKO2d1xozclOzgjXPYovJJIultzkMu34qQb9Sz/yilrbCgj8=\n"
"-----END CERTIFICATE-----\n";

// ============================================================================
// Device configuration — single in-memory instance
// ============================================================================

app_config_t _app_config;

void app_config_load_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->wifi_ssid,     "FERP-SSID",     sizeof(cfg->wifi_ssid) - 1);
    strncpy(cfg->wifi_password, "FERP-PASSWORD", sizeof(cfg->wifi_password) - 1);

    strncpy(cfg->ota_server_url, "http://144.24.156.245:8080", sizeof(cfg->ota_server_url) - 1);
    cfg->ota_check_interval_s = 30;

    strncpy(cfg->mqtt_host, "broker.emqx.io", sizeof(cfg->mqtt_host) - 1);
    cfg->mqtt_port = 1883;

    cfg->log_udp_enabled = true;
    strncpy(cfg->log_udp_server_ip, "144.24.156.245", sizeof(cfg->log_udp_server_ip) - 1);
    cfg->log_udp_port = 22222;

    // Receipt defaults = legacy printer firmware defaults
    strncpy(cfg->prn_name_l1,  "ABC",                                sizeof(cfg->prn_name_l1) - 1);
    strncpy(cfg->prn_name_l2,  "Fuel Station",                       sizeof(cfg->prn_name_l2) - 1);
    strncpy(cfg->prn_add_l1,   "No 75, ABC Road, ",                  sizeof(cfg->prn_add_l1) - 1);
    strncpy(cfg->prn_add_l2,   "Colombo",                            sizeof(cfg->prn_add_l2) - 1);
    strncpy(cfg->prn_tele,     "TELE 0123456789     FAX 0123456789", sizeof(cfg->prn_tele) - 1);
    strncpy(cfg->prn_thank_l1, "THANK YOU! COME AGAIN!",             sizeof(cfg->prn_thank_l1) - 1);
    strncpy(cfg->prn_thank_l2, "FEEDBACK 0123456789",                sizeof(cfg->prn_thank_l2) - 1);
    strncpy(cfg->prn_footer,   "www.myfuelstation.net (0712209310)", sizeof(cfg->prn_footer) - 1);
    cfg->prn_baud_rate       = 19200;
    cfg->prn_end_lines       = 4;
    cfg->prn_theme           = 1;
    cfg->prn_printer_dots    = 35;
    cfg->prn_lines_after_cut = 0;
    cfg->prn_cp_poll_s       = 60;
    cfg->prn_en_print_time   = false;
    cfg->prn_en_signature    = true;
    cfg->prn_en_cut          = false;
    cfg->prn_en_cloud_print  = false;
    cfg->prn_en_boot_print   = true;
}

#define CFG_STR(key, name, field)  { key, name, HSYS_TYPE_STRING, _app_config.field, sizeof(_app_config.field) }
#define CFG_U32(key, name, field)  { key, name, HSYS_TYPE_UINT32, &_app_config.field, sizeof(_app_config.field) }
#define CFG_BOOL(key, name, field) { key, name, HSYS_TYPE_BOOL,   &_app_config.field, sizeof(_app_config.field) }

static config_t k_config_table[] = {
    // Network / platform (same names as the ferp-com products and the legacy printer)
    CFG_STR (CFG_KEY_WIFI_SSID,            "ssid",           wifi_ssid),
    CFG_STR (CFG_KEY_WIFI_PASSWORD,        "password",       wifi_password),
    CFG_STR (CFG_KEY_OTA_SERVER_URL,       "ota_srvr_url",   ota_server_url),
    CFG_U32 (CFG_KEY_OTA_CHECK_INTERVAL_S, "ota_chk_int",    ota_check_interval_s),
    CFG_STR (CFG_KEY_MQTT_HOST,            "mqtt_host",      mqtt_host),
    CFG_U32 (CFG_KEY_MQTT_PORT,            "mqtt_port",      mqtt_port),
    CFG_STR (CFG_KEY_MQTT_USER,            "mqtt_user",      mqtt_user),
    CFG_STR (CFG_KEY_MQTT_PASSWORD,        "mqtt_pass",      mqtt_password),
    CFG_BOOL(CFG_KEY_LOG_UDP_ENABLED,      "en_udp_ser",     log_udp_enabled),
    CFG_STR (CFG_KEY_LOG_UDP_SERVER_IP,    "udp_srvr_ip",    log_udp_server_ip),
    CFG_U32 (CFG_KEY_LOG_UDP_PORT,         "udp_srvr_port",  log_udp_port),

    // Receipt printer — legacy printer JSON names
    CFG_STR (CFG_KEY_PRN_NAME_L1,          "name_l1",        prn_name_l1),
    CFG_STR (CFG_KEY_PRN_NAME_L2,          "name_l2",        prn_name_l2),
    CFG_STR (CFG_KEY_PRN_NAME_L3,          "name_l3",        prn_name_l3),
    CFG_STR (CFG_KEY_PRN_ADD_L1,           "add_l1",         prn_add_l1),
    CFG_STR (CFG_KEY_PRN_ADD_L2,           "add_l2",         prn_add_l2),
    CFG_STR (CFG_KEY_PRN_ADD_L3,           "add_l3",         prn_add_l3),
    CFG_STR (CFG_KEY_PRN_TELE,             "tele",           prn_tele),
    CFG_STR (CFG_KEY_PRN_THANK_L1,         "thank_l1",       prn_thank_l1),
    CFG_STR (CFG_KEY_PRN_THANK_L2,         "thank_l2",       prn_thank_l2),
    CFG_STR (CFG_KEY_PRN_THANK_L3,         "thank_l3",       prn_thank_l3),
    CFG_STR (CFG_KEY_PRN_THANK_L4,         "thank_l4",       prn_thank_l4),
    CFG_STR (CFG_KEY_PRN_THANK_L5,         "thank_l5",       prn_thank_l5),
    CFG_STR (CFG_KEY_PRN_THANK_L6,         "thank_l6",       prn_thank_l6),
    CFG_STR (CFG_KEY_PRN_FOOTER,           "footer",         prn_footer),
    CFG_U32 (CFG_KEY_PRN_END_LINES,        "end_lines",      prn_end_lines),
    CFG_U32 (CFG_KEY_PRN_BAUD_RATE,        "baud_rate",      prn_baud_rate),
    CFG_BOOL(CFG_KEY_PRN_EN_PRINT_TIME,    "en_print_t",     prn_en_print_time),
    CFG_BOOL(CFG_KEY_PRN_EN_SIGNATURE,     "en_signature",   prn_en_signature),
    CFG_U32 (CFG_KEY_PRN_THEME,            "theme",          prn_theme),
    CFG_BOOL(CFG_KEY_PRN_EN_CUT,           "en_cut",         prn_en_cut),
    CFG_U32 (CFG_KEY_PRN_LINES_AFTER_CUT,  "lines_aftr_cut", prn_lines_after_cut),
    CFG_U32 (CFG_KEY_PRN_PRINTER_DOTS,     "printer_dots",   prn_printer_dots),
    CFG_BOOL(CFG_KEY_PRN_EN_CLOUD_PRINT,   "en_cloud_print", prn_en_cloud_print),
    CFG_U32 (CFG_KEY_PRN_CP_POLL_S,        "cp_poll_s",      prn_cp_poll_s),
    CFG_BOOL(CFG_KEY_PRN_EN_BOOT_PRINT,    "en_boot_print",  prn_en_boot_print),
};
#define CONFIG_TABLE_SIZE  (sizeof(k_config_table) / sizeof(k_config_table[0]))

const app_config_t *app_config_get(void)
{
    return &_app_config;
}

config_t *app_config_get_table(uint16_t *out_size)
{
    if (out_size) *out_size = (uint16_t)CONFIG_TABLE_SIZE;
    return k_config_table;
}

// ============================================================================
// Device identity — runtime-only, never persisted to flash
// ============================================================================

#define APP_DEVICE_GROUP  "default"

static app_device_info_t s_device_info = {
    .device_uuid      = {},
    .device_group     = APP_DEVICE_GROUP,
    .hw_address       = {},
    .fw_version       = FW_VERSION,
    .hw_version       = HW_VERSION,
    .disp_tap_version = {},
};

// On printers the cloud identity is provisioned by ModuleCloudPrint.
const hsys_module_id_t k_dev_info_perm_cloud_write[]    = { MODULE_CLOUD_PRINT_ID };
const uint8_t          k_dev_info_perm_cloud_write_count = 1;

const hsys_module_id_t k_dev_info_perm_ota_write[]         = { MODULE_OTA_ID };
const uint8_t          k_dev_info_perm_ota_write_count      = 1;

static dev_info_entry_t k_dev_info_table[] = {
    { DEV_INFO_KEY_DEVICE_UUID,  "device_uuid",  k_dev_info_perm_cloud_write, k_dev_info_perm_cloud_write_count,
      nullptr, 0, HSYS_TYPE_STRING, s_device_info.device_uuid,  sizeof(s_device_info.device_uuid),  false },
    { DEV_INFO_KEY_DEVICE_GROUP, "device_group", k_dev_info_perm_cloud_write, k_dev_info_perm_cloud_write_count,
      nullptr, 0, HSYS_TYPE_STRING, s_device_info.device_group, sizeof(s_device_info.device_group), true  },
    { DEV_INFO_KEY_HW_ADDRESS,   "hw_address",   nullptr, 0,
      nullptr, 0, HSYS_TYPE_STRING, s_device_info.hw_address,   sizeof(s_device_info.hw_address),   false },
    { DEV_INFO_KEY_FW_VERSION,   "fw_version",   nullptr, 0,
      nullptr, 0, HSYS_TYPE_STRING, s_device_info.fw_version,   sizeof(s_device_info.fw_version),   true  },
    { DEV_INFO_KEY_HW_VERSION,   "hw_version",   nullptr, 0,
      nullptr, 0, HSYS_TYPE_STRING, s_device_info.hw_version,   sizeof(s_device_info.hw_version),   true  },
};
#define DEV_INFO_TABLE_SIZE  (sizeof(k_dev_info_table) / sizeof(k_dev_info_table[0]))

app_device_info_t *app_device_info_get(void)
{
    return &s_device_info;
}

dev_info_entry_t *app_device_info_get_table(uint16_t *out_count)
{
    if (out_count) *out_count = (uint16_t)DEV_INFO_TABLE_SIZE;
    return k_dev_info_table;
}

// ============================================================================
// Codec table — JSON serialisation registry (wire-capable messages)
// ============================================================================

static const app_msg_codec_entry_t k_codec_table[] = {
    // ── Config requests / responses ──────────────────────────────────────────
    { "MsgConfigGetMqtt",        MSG_ID_CONFIG_GET_MQTT,       MsgConfigGetMqtt::from_json,        MsgConfigGetMqtt::to_json       },
    { "MsgConfigGetWifi",        MSG_ID_CONFIG_GET_WIFI,       MsgConfigGetWifi::from_json,        MsgConfigGetWifi::to_json       },
    { "MsgConfigGetCloud",       MSG_ID_CONFIG_GET_CLOUD,      MsgConfigGetCloud::from_json,       MsgConfigGetCloud::to_json      },
    { "MsgConfigGetOta",         MSG_ID_CONFIG_GET_OTA,        MsgConfigGetOta::from_json,         MsgConfigGetOta::to_json        },
    { "MsgConfigGet",            MSG_ID_CONFIG_GET,            MsgConfigGet::from_json,            MsgConfigGet::to_json           },
    { "MsgConfigGetKey",         MSG_ID_CONFIG_GET_KEY,        MsgConfigGetKey::from_json,         MsgConfigGetKey::to_json        },
    { "MsgConfigSet",            MSG_ID_CONFIG_SET,            MsgConfigSet::from_json,            MsgConfigSet::to_json           },
    { "MsgConfigMqtt",           MSG_ID_CONFIG_MQTT,           MsgConfigMqtt::from_json,           MsgConfigMqtt::to_json          },
    { "MsgConfigWifi",           MSG_ID_CONFIG_WIFI,           MsgConfigWifi::from_json,           MsgConfigWifi::to_json          },
    { "MsgConfigCloud",          MSG_ID_CONFIG_CLOUD,          MsgConfigCloud::from_json,          MsgConfigCloud::to_json         },
    { "MsgConfigOta",            MSG_ID_CONFIG_OTA,            MsgConfigOta::from_json,            MsgConfigOta::to_json           },
    { "MsgConfigReady",          MSG_ID_CONFIG_READY,          MsgConfigReady::from_json,          MsgConfigReady::to_json         },
    { "MsgConfigValue",          MSG_ID_CONFIG_VALUE,          MsgConfigValue::from_json,          MsgConfigValue::to_json         },

    // ── Storage / system ─────────────────────────────────────────────────────
    { "MsgSpiffsReady",          MSG_ID_SPIFFS_READY,          MsgSpiffsReady::from_json,          MsgSpiffsReady::to_json         },
    { "MsgSpiffsCleanup",        MSG_ID_SPIFFS_CLEANUP,        MsgSpiffsCleanup::from_json,        MsgSpiffsCleanup::to_json       },
    { "MsgSystemReboot",         MSG_ID_SYSTEM_REBOOT,         MsgSystemReboot::from_json,         MsgSystemReboot::to_json        },

    // ── Connectivity ─────────────────────────────────────────────────────────
    { "MsgWifiEvent",            MSG_ID_WIFI_EVENT,            MsgWifiEvent::from_json,            MsgWifiEvent::to_json           },
    { "MsgInternetStatus",       MSG_ID_INTERNET_STATUS,       MsgInternetStatus::from_json,       MsgInternetStatus::to_json      },
    { "MsgMqttStatus",           MSG_ID_MQTT_STATUS,           MsgMqttStatus::from_json,           MsgMqttStatus::to_json          },

    // ── OTA lifecycle ────────────────────────────────────────────────────────
    { "MsgOtaStartRequest",      MSG_ID_OTA_START_REQUEST,     MsgOtaStartRequest::from_json,      MsgOtaStartRequest::to_json     },
    { "MsgOtaAbortRequest",      MSG_ID_OTA_ABORT_REQUEST,     MsgOtaAbortRequest::from_json,      MsgOtaAbortRequest::to_json     },
    { "MsgOtaStartResponse",     MSG_ID_OTA_START_RESPONSE,    MsgOtaStartResponse::from_json,     MsgOtaStartResponse::to_json    },
    { "MsgOtaCompleteNotify",    MSG_ID_OTA_COMPLETE_NOTIFY,   MsgOtaCompleteNotify::from_json,    MsgOtaCompleteNotify::to_json   },
    { "MsgOtaRequestDriver",     MSG_ID_OTA_REQUEST_DRIVER,    MsgOtaRequestDriver::from_json,     MsgOtaRequestDriver::to_json    },
    { "MsgOtaEvent",             MSG_ID_OTA_EVENT,             MsgOtaEvent::from_json,             MsgOtaEvent::to_json            },
    { "MsgOtaProgress",          MSG_ID_OTA_PROGRESS,          MsgOtaProgress::from_json,          MsgOtaProgress::to_json         },

    // ── Timers ───────────────────────────────────────────────────────────────
    { "MsgTimerStart",           MSG_ID_TIMER_START,           MsgTimerStart::from_json,           MsgTimerStart::to_json          },
    { "MsgTimerStop",            MSG_ID_TIMER_STOP,            MsgTimerStop::from_json,            MsgTimerStop::to_json           },
    { "MsgTimerStartResponse",   MSG_ID_TIMER_START_RESPONSE,  MsgTimerStartResponse::from_json,   MsgTimerStartResponse::to_json  },
    { "MsgTimerStopResponse",    MSG_ID_TIMER_STOP_RESPONSE,   MsgTimerStopResponse::from_json,    MsgTimerStopResponse::to_json   },
    { "MsgTimerAlarm",           MSG_ID_TIMER_ALARM,           MsgTimerAlarm::from_json,           MsgTimerAlarm::to_json          },
    { "MsgTick1000ms",           MSG_ID_TICK_1000MS,           MsgTick1000ms::from_json,           MsgTick1000ms::to_json          },

    // ── Device info / diagnostics ────────────────────────────────────────────
    { "MsgDevInfoRead",          MSG_ID_DEV_INFO_READ,         MsgDevInfoRead::from_json,          MsgDevInfoRead::to_json         },
    { "MsgDevInfoValue",         MSG_ID_DEV_INFO_VALUE,        MsgDevInfoValue::from_json,         MsgDevInfoValue::to_json        },
    { "MsgPoolGetJson",          MSG_ID_POOL_GET_JSON,         MsgPoolGetJson::from_json,          MsgPoolGetJson::to_json         },
    { "MsgPoolJson",             MSG_ID_POOL_JSON,             MsgPoolJson::from_json,             MsgPoolJson::to_json            },
    { "MsgGetFileListSpiffs",    MSG_ID_GET_FILE_LIST_SPIFFS,  MsgGetFileListSpiffs::from_json,    MsgGetFileListSpiffs::to_json   },
    { "MsgFileListSpiffs",       MSG_ID_FILE_LIST_SPIFFS,      MsgFileListSpiffs::from_json,       MsgFileListSpiffs::to_json      },

    // ── Receipt printer ──────────────────────────────────────────────────────
    { "MsgPrintJob",             MSG_ID_PRINT_JOB,             MsgPrintJob::from_json,             MsgPrintJob::to_json            },
    { "MsgPrintResult",          MSG_ID_PRINT_RESULT,          MsgPrintResult::from_json,          MsgPrintResult::to_json         },
    { "MsgPrinterCmd",           MSG_ID_PRINTER_CMD,           MsgPrinterCmd::from_json,           MsgPrinterCmd::to_json          },
    { "MsgPrinterGetStatus",     MSG_ID_PRINTER_GET_STATUS,    MsgPrinterGetStatus::from_json,     MsgPrinterGetStatus::to_json    },
    { "MsgPrinterStatus",        MSG_ID_PRINTER_STATUS,        MsgPrinterStatus::from_json,        MsgPrinterStatus::to_json       },
    { "MsgCloudPrintGetStatus",  MSG_ID_CLOUD_PRINT_GET_STATUS, MsgCloudPrintGetStatus::from_json, MsgCloudPrintGetStatus::to_json },
    { "MsgCloudPrintStatus",     MSG_ID_CLOUD_PRINT_STATUS,    MsgCloudPrintStatus::from_json,     MsgCloudPrintStatus::to_json    },
};

// ============================================================================
// MQTT route table — inbound routing policy for ModuleMqtt
//   dest_module = 0 → publish (notification), otherwise DIRECT send
// ============================================================================

static const app_msg_mqtt_route_t k_mqtt_route_table[] = {
    //  msg_id                          dest_module            multicast_resp
    { MSG_ID_CONFIG_GET_MQTT,         MODULE_CONFIG_ID,      false },
    { MSG_ID_CONFIG_GET_WIFI,         MODULE_CONFIG_ID,      false },
    { MSG_ID_CONFIG_GET_CLOUD,        MODULE_CONFIG_ID,      false },
    { MSG_ID_CONFIG_GET_OTA,          MODULE_CONFIG_ID,      false },
    { MSG_ID_CONFIG_GET,              MODULE_CONFIG_ID,      false },
    { MSG_ID_CONFIG_GET_KEY,          MODULE_CONFIG_ID,      false },
    { MSG_ID_CONFIG_SET,              (hsys_module_id_t)0,   true  },

    { MSG_ID_WIFI_EVENT,              (hsys_module_id_t)0,   false },
    { MSG_ID_INTERNET_STATUS,         (hsys_module_id_t)0,   false },

    { MSG_ID_OTA_START_REQUEST,       (hsys_module_id_t)0,   false },
    { MSG_ID_OTA_ABORT_REQUEST,       (hsys_module_id_t)0,   false },
    { MSG_ID_OTA_START_RESPONSE,      (hsys_module_id_t)0,   false },
    { MSG_ID_OTA_COMPLETE_NOTIFY,     (hsys_module_id_t)0,   false },
    { MSG_ID_OTA_REQUEST_DRIVER,      (hsys_module_id_t)0,   false },

    { MSG_ID_TIMER_START,             MODULE_TIMER_ID,       false },
    { MSG_ID_TIMER_STOP,              MODULE_TIMER_ID,       false },

    { MSG_ID_DEV_INFO_READ,           MODULE_DEVICE_INFO_ID, false },
    { MSG_ID_POOL_GET_JSON,           (hsys_module_id_t)0,   false },
    { MSG_ID_GET_FILE_LIST_SPIFFS,    (hsys_module_id_t)0,   false },
    { MSG_ID_SYSTEM_REBOOT,           MODULE_SYSMON_ID,      false },

    // ── Receipt printer ──────────────────────────────────────────────────────
    { MSG_ID_PRINT_JOB,               MODULE_PRINTER_ID,     false },
    { MSG_ID_PRINTER_CMD,             MODULE_PRINTER_ID,     false },
    { MSG_ID_PRINTER_GET_STATUS,      MODULE_PRINTER_ID,     false },
    { MSG_ID_CLOUD_PRINT_GET_STATUS,  MODULE_CLOUD_PRINT_ID, false },
};

// Printer replies forwarded by ModuleMqtt (DIRECT replies → resp topic)
static const mqtt_outbound_msg_t k_mqtt_outbound[] = {
    { MSG_ID_PRINT_RESULT,        false },
    { MSG_ID_PRINTER_STATUS,      false },
    { MSG_ID_CLOUD_PRINT_STATUS,  false },
};

// ============================================================================
// OTA platform configuration — one target: this application image
// ============================================================================

static ota_esp32_ctx_t s_esp32_ota_ctx = {};

static const ota_source_desc_t k_ota_sources[] = {
    // source_module_id             priority  _pad  timeout_ms
    { MODULE_MQTT_ID,              0,        0,     60000 },
    { MODULE_WEB_SERVER_ID,        0,        0,    120000 },  ///< web OTA (port 80, incl. /updatePrinterFirmwareBin)
    { MODULE_WEB_CLIENT_OTA_ID,    0,        0,    120000 },  ///< cloud-polling OTA
};

#define OTA_TARGET_MAIN_IDX      0

// A product-specific target name means a ferp-com bundle can never be
// flashed onto a printer (and vice versa) — the device rejects the target.
static const ota_target_desc_t k_ota_targets[] = {
    { OTA_TARGET_MAIN_IDX, true, {}, PRINTER_OTA_TARGET, &g_ota_driver_esp32_main, &s_esp32_ota_ctx },
};

static const mqtt_ota_target_t k_mqtt_ota_targets[] = {
    { PRINTER_OTA_TARGET, OTA_TARGET_MAIN_IDX },
};

// ============================================================================
// Web server tables (port 80 — legacy printer API + standard /api/ routes)
// ============================================================================

static size_t s_spiffs_read_offset = 0;
static int32_t _web_spiffs_read(const char *path, uint8_t *buf,
                                 size_t buf_size, size_t *bytes_read, void *ctx)
{
    size_t *offset = static_cast<size_t *>(ctx);
    int32_t rc = app_spiffs_read_file_at(path, *offset, (char *)buf, buf_size, bytes_read, 1000);
    if (rc != APP_SPIFFS_OK) {
        *offset = 0;
        return rc;
    }
    if (*bytes_read < buf_size) {
        *offset = 0;        // EOF reached — reset for next request
    } else {
        *offset += *bytes_read;
    }
    return APP_SPIFFS_OK;
}
static const pal_http_file_driver_t k_spiffs_driver = { _web_spiffs_read, &s_spiffs_read_offset };

// The legacy printer pages already live in SPIFFS on upgraded units.
static const ModuleWebServer::StaticFileDef k_web_pages[] = {
    { "/",                      "index.html",             &k_spiffs_driver },
    { "/index.html",            "index.html",             &k_spiffs_driver },
    { "/styles.css",            "styles.css",             &k_spiffs_driver },
    { "/deviceConfigurations",  "deviceConfPrinter.html", &k_spiffs_driver },
    { "/about",                 "about.html",             &k_spiffs_driver },
    { "/contact",               "contact.html",           &k_spiffs_driver },
    { nullptr, nullptr, nullptr }
};

static const ModuleWebServer::OtaTargetDef k_web_ota_bins[] = {
    { PRINTER_OTA_TARGET, OTA_TARGET_MAIN_IDX },
    { "esp32-main",       OTA_TARGET_MAIN_IDX },   ///< alias for tools that only know the ferp-com name
    { nullptr, 0 }
};

// Legacy firmware-upload URI: curl -F "file=@fw.bin" http://<ip>/updatePrinterFirmwareBin
static const char *const k_fw_upload_aliases[] = {
    "/updatePrinterFirmwareBin",
    nullptr
};

static const ModuleWebServer::ApiMsgRouteDef k_api_routes[] = {
    //  msg_id                          dest_module            response_id
    { MSG_ID_CONFIG_GET_MQTT,         MODULE_CONFIG_ID,      MSG_ID_CONFIG_MQTT         },
    { MSG_ID_CONFIG_GET_WIFI,         MODULE_CONFIG_ID,      MSG_ID_CONFIG_WIFI         },
    { MSG_ID_CONFIG_GET_CLOUD,        MODULE_CONFIG_ID,      MSG_ID_CONFIG_CLOUD        },
    { MSG_ID_CONFIG_GET_OTA,          MODULE_CONFIG_ID,      MSG_ID_CONFIG_OTA          },
    { MSG_ID_CONFIG_GET_KEY,          MODULE_CONFIG_ID,      MSG_ID_CONFIG_VALUE        },
    { MSG_ID_CONFIG_SET,              (hsys_module_id_t)0,   (hsys_msg_id_t)0           },
    { MSG_ID_DEV_INFO_READ,           MODULE_DEVICE_INFO_ID, MSG_ID_DEV_INFO_VALUE      },
    { MSG_ID_POOL_GET_JSON,           (hsys_module_id_t)0,   MSG_ID_POOL_JSON           },
    { MSG_ID_GET_FILE_LIST_SPIFFS,    (hsys_module_id_t)0,   MSG_ID_FILE_LIST_SPIFFS    },
    { MSG_ID_PRINT_JOB,               MODULE_PRINTER_ID,     MSG_ID_PRINT_RESULT        },
    { MSG_ID_PRINTER_CMD,             MODULE_PRINTER_ID,     MSG_ID_PRINTER_STATUS      },
    { MSG_ID_PRINTER_GET_STATUS,      MODULE_PRINTER_ID,     MSG_ID_PRINTER_STATUS      },
    { MSG_ID_CLOUD_PRINT_GET_STATUS,  MODULE_CLOUD_PRINT_ID, MSG_ID_CLOUD_PRINT_STATUS  },
    { (hsys_msg_id_t)0,               (hsys_module_id_t)0,   (hsys_msg_id_t)0           }
};

// ============================================================================
// Memory pool
// ============================================================================

static constexpr hsys_pool_class_cfg_t k_pool_table[] = {
    {    4,   8 },
    {   32,  32 },
    {   64,  32 },
    {  256,  24 },   ///< MsgPrintJob (224 B), MsgMqttExtData (256 B), status replies
    {  512,   8 },
    { 2048,   4 },   ///< MsgHttpRequest data buffer
    { 4096,   2 },   ///< ModuleConfig JSON working buffer (MODULE_CONFIG_JSON_BUF_SIZE)
};
#define POOL_TABLE_SIZE  (sizeof(k_pool_table) / sizeof(k_pool_table[0]))

// hsys_pool_init() refuses a table larger than the static arena, and then no
// message can be allocated at all — catch that at compile time.
static constexpr uint32_t _pool_bytes(size_t i = 0)
{
    return i < POOL_TABLE_SIZE
        ? (uint32_t)k_pool_table[i].block_size * k_pool_table[i].block_count + _pool_bytes(i + 1)
        : 0;
}
static_assert(_pool_bytes() <= HSYS_POOL_MAX_BYTES, "pool table exceeds HSYS_POOL_MAX_BYTES (user_config.h)");

// ============================================================================
// Printer transport (selected by app_hw_config.h)
// ============================================================================

#if defined(PRINTER_TRANSPORT_USB)
static PrinterTransportUsb  s_printer_transport;
#else
static PrinterTransportUart s_printer_transport(PRINTER_UART_PORT, PRINTER_UART_TX_GPIO, PRINTER_UART_RX_GPIO);
#endif

// ============================================================================
// Module / task tables
// ============================================================================

static HsysModule *k_module_table[] = {
    Ticker::instance(),
    ModuleSysmon::instance(),
    ModuleSpiffs::instance(),
    ModuleConfig::instance(),
    ModuleTimer::instance(),
    ModuleInternet::instance(),
    ModuleWifi::instance(),
    OtaModule::instance(),
    ModuleWebClientOta::instance(),
    ModuleWebServer::instance(),
    ModuleMqtt::instance(),
    ModuleDeviceInfo::instance(),
    ModuleHttp::instance(),
    ModuleUdpLog::instance(),
    ModulePrinter::instance(),
    ModulePrinterApi::instance(),
    ModuleCloudPrint::instance(),
};
#define MODULE_TABLE_SIZE  (sizeof(k_module_table) / sizeof(k_module_table[0]))

static const hsys_task_desc_t k_task_table[] = {
    //  name            stack     prio  queue  modules
    // storage_task : SPIFFS mount + JSON config (4 KB pool buffer, heap JSON docs)
    { "storage_task",   6*1024,  5,  0,   { MODULE_SPIFFS_ID,  MODULE_CONFIG_ID,  MODULE_DEVICE_INFO_ID,  0 } },
    { "timing_task",    3*1024,  4,  0,   { TICKER_MODULE_ID,  MODULE_TIMER_ID,                           0 } },
    { "indicator_task", 2*1024,  4,  0,   { MODULE_SYSMON_ID,                                             0 } },
    // printer_task : blocking ESC/POS writes (~1 s per receipt at 9600 baud); render buffer is static
    // PrinterApi only sends messages (its HTTP handlers run on the httpd task) — it shares this task.
    { "printer_task",   5*1024,  5, 16,   { MODULE_PRINTER_ID,  MODULE_PRINTER_API_ID,                    0 } },
    // network_task : WiFi + ping + MQTT + web server + OTA sources + cloud print state machine
    { "network_task",  10*1024,  5, 32,   { MODULE_WIFI_ID,       MODULE_INTERNET_ID,  MODULE_MQTT_ID,
                                            MODULE_WEB_CLIENT_OTA_ID, MODULE_WEB_SERVER_ID, MODULE_OTA_ID,
                                            MODULE_UDP_LOG_ID,    MODULE_CLOUD_PRINT_ID,                      0 } },
    // http_task    : ModuleHttp owns every TLS session (cloud print) → 10 KB
    { "http_task",     10*1024,  5,  0,   { MODULE_HTTP_ID,                                               0 } },
};
#define TASK_TABLE_SIZE  (sizeof(k_task_table) / sizeof(k_task_table[0]))

static_assert(TASK_TABLE_SIZE <= HSYS_MAX_TASKS,
              "TASK_TABLE_SIZE exceeds HSYS_MAX_TASKS; increase HSYS_MAX_TASKS in user_config.h");

// ============================================================================
// Extra module injection (called by app_platform_pre_init)
// ============================================================================

#define APP_MAX_EXTRA_MODULES  4

static HsysModule              *s_extra_modules[APP_MAX_EXTRA_MODULES] = {};
static const hsys_task_desc_t  *s_extra_tasks[APP_MAX_EXTRA_MODULES]   = {};
static uint8_t                  s_extra_count = 0;

extern "C" void app_register_extra_module(HsysModule             *module,
                                           const hsys_task_desc_t *task_desc)
{
    if (!module || !task_desc || s_extra_count >= APP_MAX_EXTRA_MODULES) return;
    s_extra_modules[s_extra_count] = module;
    s_extra_tasks[s_extra_count]   = task_desc;
    s_extra_count++;
}

extern "C" __attribute__((weak)) void app_platform_pre_init(void) {}

extern "C" void app_config_init(void)
{
    app_config_load_defaults(&_app_config);
}

// ============================================================================
// app_init
// ============================================================================

extern "C" void app_init(void)
{
    logger.init();

    pal_system_init();
    pal_crash_log_init();
    app_platform_pre_init();

    app_msg_codec_register(k_codec_table,
                           (uint8_t)(sizeof(k_codec_table) / sizeof(k_codec_table[0])));
    app_msg_mqtt_route_register(k_mqtt_route_table,
                                (uint8_t)(sizeof(k_mqtt_route_table) / sizeof(k_mqtt_route_table[0])));

    // Printer
    ModulePrinter::instance()->set_transport(&s_printer_transport);
#if defined(PRINTER_BUZZER_GPIO)
    ModulePrinter::instance()->set_buzzer_gpio(PRINTER_BUZZER_GPIO);
#endif
    ModuleCloudPrint::instance()->set_root_ca(global_root_ca);

    // Web server: legacy port + legacy API
    ModuleWebServer::instance()->set_port(PRINTER_HTTP_PORT);
    ModuleWebServer::instance()->set_static_files(k_web_pages);
    ModuleWebServer::instance()->set_ota_targets(k_web_ota_bins);
    ModuleWebServer::instance()->set_api_routes(k_api_routes);
    ModuleWebServer::instance()->set_extra_routes(ModulePrinterApi::routes());
    ModuleWebServer::instance()->set_fw_upload_aliases(k_fw_upload_aliases, PRINTER_OTA_TARGET);

    // OTA
    OtaModule::instance()->set_platform_config(
        k_ota_sources, (uint8_t)(sizeof(k_ota_sources) / sizeof(k_ota_sources[0])),
        k_ota_targets, (uint8_t)(sizeof(k_ota_targets) / sizeof(k_ota_targets[0])));

    // MQTT: own device type + printer replies + OTA target names
    ModuleMqtt::instance()->set_dev_type(PRINTER_MQTT_DEV_TYPE);
    ModuleMqtt::instance()->set_outbound_msgs(
        k_mqtt_outbound, (uint8_t)(sizeof(k_mqtt_outbound) / sizeof(k_mqtt_outbound[0])));
    ModuleMqtt::instance()->set_ota_targets(
        k_mqtt_ota_targets, (uint8_t)(sizeof(k_mqtt_ota_targets) / sizeof(k_mqtt_ota_targets[0])));

    // 1. Config defaults
    app_config_init();

    // 2. Memory pool
    hsys_pool_init(k_pool_table, POOL_TABLE_SIZE);

    // 3. Module registry
    {
        HsysModule *all_modules[MODULE_TABLE_SIZE + APP_MAX_EXTRA_MODULES];
        memcpy(all_modules, k_module_table, sizeof(HsysModule *) * MODULE_TABLE_SIZE);
        for (uint8_t i = 0; i < s_extra_count; i++)
            all_modules[MODULE_TABLE_SIZE + i] = s_extra_modules[i];
        hsys_module_init(all_modules, (uint8_t)(MODULE_TABLE_SIZE + s_extra_count));
    }

    // 4. Message bus + descriptor table
    hsys_msg_init();
    APP_MSG_TABLE_INIT;
    hsys_msg_table_init(k_msg_table, k_msg_table_count);

    // 5. Task manager
    {
        hsys_task_desc_t all_tasks[TASK_TABLE_SIZE + APP_MAX_EXTRA_MODULES];
        memcpy(all_tasks, k_task_table, sizeof(hsys_task_desc_t) * TASK_TABLE_SIZE);
        for (uint8_t i = 0; i < s_extra_count; i++)
            all_tasks[TASK_TABLE_SIZE + i] = *s_extra_tasks[i];
        hsys_task_mgr_init(all_tasks, (uint8_t)(TASK_TABLE_SIZE + s_extra_count));
    }
}

extern "C" void app_run(void)
{
    hsys_task_delay(1000);
}
