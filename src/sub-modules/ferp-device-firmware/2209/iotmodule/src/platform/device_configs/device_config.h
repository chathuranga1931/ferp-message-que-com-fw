
#ifndef __DEVICE_CONFIG_H
#define __DEVICE_CONFIG_H

#include "ESPAsyncWebServer.h"
#include "time.h"
#include "nozzel_event.h"

#define SIZE_OF_NTWK_BASE_URL 	   (150)
#define SIZE_OF_NTWK_ACCESS_TOKEN	(50)
#define SIZE_OF_WIFI_SSID			(50)
#define SIZE_OF_WIFI_PASSWORD		(50)
#define SIZE_OF_SECRET				(50)
#define SIZE_OF_UUID				(50)
#define SIZE_OF_NOZZELID			(10)
#define SIZE_OF_FUEL_TYPE			(10)
#define SIZE_OF_MAC					(25)
#define SIZE_OF_IPADDRESS			(25)
#define SIZE_OF_FUEL_TYPE_STR		(35)

#define DEFAULT_CLOUD_URL 		("https://http-ingress-alw5epn3aq-el.a.run.app/api/v1/device/data")
#define DEFAULT_CLOUD_SECRET 	("")
#define DEFAULT_WIFI_SSID 		("HUAWEI-B310-98DE")
#define DEFAULT_WIFI_PW 		("CHATHU**123")
#define DEFAULT_UUID 			("3df3176c-9d64-4b48-8651-5ef503ebaabb")
#define DEFAULT_NOZZEL_ID 		("01")
#define DEFAULT_FUEL_TYPE 		("100")
#define DEFAULT_FUEL_TYPE_STR	("Petrol 95")
#define DEFAULT_PRINTER_URL		("http://192.168.1.5/print")

typedef struct {
	char  url[SIZE_OF_NTWK_BASE_URL]; // = "https://http-ingress-alw5epn3aq-el.a.run.app/api/v1/device/data";
	char  secret[SIZE_OF_SECRET];
} network_configs_t;

typedef struct {
	char ssid[SIZE_OF_WIFI_SSID] = {0};
	char password[SIZE_OF_WIFI_PASSWORD] = {0};
	unsigned int wait_time_s = 7;
} wifi_configs_t;

enum device_reset_reason_t{
	rst_reason_wifi_not_connected = 0,
	rst_reason_unknown,
};

enum config_file_status_t{
	config_file_not_available,
	config_file_loaded,
};

enum wifi_status_t{
	wifi_status_pending,
	wifi_status_connected,
	wifi_status_ap_mode
};

enum date_time_status_t{
	date_time_pending,
	date_time_updated
};

enum sd_card_status_t{
	sd_card_mounted,
	sd_card_pending
};

typedef struct{

}sdcard_info_t;

enum internet_status_t{
	internet_not_tested = 0,
	internet_connected,
	internet_failed_at_bigining,
	internet_failed_at_runtime
};

enum rtc_status_t{
	rtc_not_available = 0,
	rtc_available,
};

typedef struct{
	device_reset_reason_t rst_reason;
	wifi_status_t wifi_status;
	internet_status_t internet_status;
	date_time_status_t date_time_status;
	sd_card_status_t sd_card_status;
	config_file_status_t config_file_status;
#ifdef FERP_COM
	rtc_status_t rtc_status;
#endif
}device_status_t;

typedef struct {
	unsigned int wait_time_s = 7;
	double region = 5.5;	//5:30
	String region_str = "+05:30";
}date_time_t;

typedef struct {
	char nozzel_id[SIZE_OF_NOZZELID];
	char fuel_type[SIZE_OF_FUEL_TYPE];
	char fuel_type_str[SIZE_OF_FUEL_TYPE_STR];
}nozzel_config_t;

typedef struct {
	char url [SIZE_OF_NTWK_BASE_URL]; //"http://192.168.1.5/print";	//"http://ferp-iot-printer12121212"; hostname of the device
}printer_configs_t;

typedef struct{
    double unit_price;
    double total_price;
    double volume_l;
    bool start_stop;
    bool select_p;
    bool select_l;
} display_data_t;

typedef struct {
	wifi_configs_t wifi;
	network_configs_t network; /* TODO Change this to cloud */
	printer_configs_t printer;
	device_status_t status;
	date_time_t datetime;
	sdcard_info_t sdcard_info;
	AsyncWebServer* async_server;
	char mac_address[SIZE_OF_MAC];
	char ip_address[SIZE_OF_IPADDRESS];
	char  UUID[SIZE_OF_UUID]; // = "3df3176c-9d64-4b48-8651-5ef503ebaabb";
	nozzel_config_t nozel_configs;
	display_data_t display_value_last;
	String display_value_last_str = "---";
} device_configs_t;

#endif //__DEVICE_CONFIG_H