

#ifndef CUBE_SPHERE_API_H
#define CUBE_SPHERE_API_H

#include "app.h"

typedef struct {
	char  url[SIZE_OF_NTWK_BASE_URL]; // = "https://http-ingress-alw5epn3aq-el.a.run.app/api/v1/device/data";
	// char  secret[SIZE_OF_SECRET];
	char agent_uuid[SIZE_OF_UUID];
	char  basic_authentication_base64[SIZE_OF_SECRET];
} network_configs_t;

typedef struct {
	char nozzle_id[SIZE_OF_NOZZELID];
	char fuel_type[SIZE_OF_FUEL_TYPE];
	char fuel_type_str[SIZE_OF_FUEL_TYPE_STR];
	char uuid[SIZE_OF_UUID]; // = "3df3176c-9d64-4b48-8651-5ef503ebaabb";
}nozzel_config_t;

typedef struct {
    int8_t rssi;
    uint32_t uptime_sec;
    uint32_t nozzle_event_count_success;
    uint32_t nozzle_event_count_failure;
}heart_beat_info_t;

typedef struct {
    char ssid[SIZE_OF_WIFI_SSID];
    char password[SIZE_OF_WIFI_PASSWORD];
    char ip_address[SIZE_OF_IPADDRESS];
    int8_t rssi;
    uint32_t uptime_sec;
}reconnect_info_t;

typedef struct {
    char ssid[SIZE_OF_WIFI_SSID];
    char password[SIZE_OF_WIFI_PASSWORD];
    char ip_address[SIZE_OF_IPADDRESS];
    char mac_address_str[SIZE_OF_MAC];
    int8_t rssi;
    uint32_t nozzle_event_count_success;
    uint32_t nozzle_event_count_failure;
    char sd_card_status[SIZE_OF_STATUS_WORD_STR];
    char sd_card_size_str[SIZE_OF_INT_STR];
    char fw_version[SIZE_OF_VERSION_STR];
    char hw_version[SIZE_OF_VERSION_STR];
    char board_version[SIZE_OF_VERSION_STR];
    char device_type[SIZE_OF_DEVICE_TYPE];
    uint32_t uptime_sec;
    char esp07_fw_version[SIZE_OF_VERSION_STR];
}startup_info_t;

typedef struct {
    char device_id[SIZE_OF_UUID];
    char secret[SIZE_OF_SECRET];
}shutdown_info_t;

typedef struct {
    uint8_t n_idx;
    uint64_t time_stamp;
    uint32_t unit_pricex100;
    uint64_t total_pricex100;
    uint64_t volume_lx1000;
    uint32_t event_id;
}pumped_event_info_t;

int32_t cube_sphere_register(char * mac_address_str_cloud_id, const char* root_ca);
int32_t cube_sphere_get_nozzle_config(nozzel_config_t * nozzle_config);
int32_t cube_sphere_send_hb(heart_beat_info_t hb);
int32_t cube_sphere_send_reconnect(reconnect_info_t reconnect);
int32_t cube_sphere_send_pumped(pumped_event_info_t nozzle_event);
int32_t cube_sphere_send_startup(startup_info_t startup);
int32_t cube_sphere_send_printed(pumped_event_info_t nozzle_event);
int32_t cube_sphere_send_status_updated(startup_info_t startup);

int32_t cube_sphere_get_device_uuid(char* device_uuid, uint32_t len);


#endif // CUBE_SPHERE_API_H