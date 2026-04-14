

#include <ArduinoJson.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "app.h"

#include "cube_sphere_api.h"

#include "pal_logger.h"
#include "pal_http_client.h"
#include "pal_crypto.h"
#include "pal_ntp.h"
#include "pal_time.h"
#include "user_config.h"

#define __TAG__  "APP_CSP "

#define CSP_DEBUG_LOG_EN      LOG_EN

static const char key[] = {"y4M5oJVfjAWeN059p\0"};

static network_configs_t _network_config = {0};
static nozzel_config_t _nozzle_config[NO_NOZZELS] = {0};
static const char * _root_ca = nullptr;

// Helper function to format time in ISO8601 with timezone
static void _format_iso8601_time(time_t epoch_sec, const char* tz_offset, char* buffer, size_t buffer_len) {
    struct tm timeinfo;
    gmtime_r(&epoch_sec, &timeinfo);
    
    // Format: 2023-09-28T18:58:07.002+05:30
    snprintf(buffer, buffer_len, "%04d-%02d-%02dT%02d:%02d:%02d.000%s",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec,
             tz_offset);
}

int32_t cube_sphere_send_event(const char* payload)
{
    int32_t ret = ERROR_OK;
    
    // Configure HTTP client
    pal_http_client_config_t http_config = {0};
    http_config.url = "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/ingress/core/v1/device/event";
    http_config.cert_pem = _root_ca;
    http_config.timeout_ms = 10000;
    http_config.keep_alive = false;
    
    pal_http_client_handle_t http_handle = NULL;
    if (pal_http_client_init(&http_config, &http_handle) != 0) {
        LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "Failed to initialize HTTP client");
        return -1;
    }
    
    // Build authentication header
    char auth_header[512] = {0};
    snprintf(auth_header, sizeof(auth_header), "Basic %s", _network_config.basic_authentication_base64);
    
    // Set headers
    pal_http_client_set_header(http_handle, "Authorization", auth_header);
    pal_http_client_set_header(http_handle, "Content-Type", "application/json");
    
    // Perform POST request
    pal_http_response_t response = {0};
    int32_t status_code = pal_http_client_post(http_handle, payload, strlen(payload), &response);
    
    if(status_code == 201 || status_code == 200){
        JsonDocument root;
        deserializeJson(root, response.body, DeserializationOption::NestingLimit(20));
        if(root.containsKey("data")){
            JsonArray results = root["data"].as<JsonArray>();
            JsonObject results_device = results[0];
            if(results_device.containsKey("status")){
                const char* status = results_device["status"].as<const char*>();
                if(strcmp(status, "OK") == 0){
                    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: Success");
                }
                else{
                    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: Failed");
                    ret = -1;
                }
            }
        }
    }
    else{
        LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "FERP-Cloud: Failed, HTTP Response code: %d", status_code);
        if (response.body != NULL) {
            LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "Response body: %s", response.body);
        }
    }
    
    // Cleanup
    pal_http_response_free(&response);
    pal_http_client_cleanup(http_handle);

    return ret;
}


static void _get_sha256_hex_string(const uint8_t * data, size_t length, char* output, size_t output_len){
    uint8_t tmp_hash[PAL_SHA256_DIGEST_LENGTH] = {0};
    pal_crypto_sha256(data, length, tmp_hash);
    pal_crypto_bin_to_hex(tmp_hash, PAL_SHA256_DIGEST_LENGTH, output, output_len);
}

static void _calc_sha256(const char* nonce, const char * mac, const char * key, char* output, size_t output_len){
    char sha256_MAC_str[PAL_SHA256_DIGEST_LENGTH * 2 + 1] = {0};
    _get_sha256_hex_string((const uint8_t *)(mac), strlen(mac), sha256_MAC_str, sizeof(sha256_MAC_str));

    // Concatenate: sha256(MAC) + key + nonce
    size_t total_len = strlen(sha256_MAC_str) + strlen(key) + strlen(nonce);
    char* input = (char*)malloc(total_len + 1);
    if (input == NULL) {
        output[0] = '\0';
        return;
    }
    
    snprintf(input, total_len + 1, "%s%s%s", sha256_MAC_str, key, nonce);
    _get_sha256_hex_string((const uint8_t *)input, strlen(input), output, output_len);
    
    free(input);
}

int32_t cube_sphere_get_nozzle_config(nozzel_config_t * nozzle_config){

    for(int i=0; i<NO_NOZZELS; i++){
        memcpy(&nozzle_config[i], &_nozzle_config[i], sizeof(nozzel_config_t));
    }
    return ERROR_OK;
}

int32_t cube_sphere_register(char * mac_address_str_cloud_id, const char* root_ca){
    int32_t ret = ERROR_APP_CLOUD_INVALID_MAC_ADDRESS;

    do{

        if(strlen(mac_address_str_cloud_id)!= 2*6){
            ret = ERROR_APP_CLOUD_INVALID_MAC_ADDRESS;
            LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "mac_address_str_cloud_id is invalid");
            break;
        }

        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "mac_address_str_cloud_id is valid");

        // Configure HTTP client
        pal_http_client_config_t http_config = {0};
        http_config.url = "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/bootstrap/core/v1/device";
        http_config.cert_pem = root_ca;
        http_config.timeout_ms = 10000;
        http_config.keep_alive = false;
        
        pal_http_client_handle_t http_handle = NULL;
        if (pal_http_client_init(&http_config, &http_handle) != 0) {
            LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "Failed to initialize HTTP client");
            ret = ERROR_APP_CLOUD_NO_NONCE;
            break;
        }
        
        // Collect www-authenticate header (try both cases)
        const char* headerNames[] = { "www-authenticate", "WWW-Authenticate" };
        pal_http_client_collect_headers(http_handle, headerNames, 2);
        
        // Perform GET to get nonce
        pal_http_response_t response = {0};
        int32_t status_code = pal_http_client_get(http_handle, &response);
        
        bool is_nonce_detected = false;        
        char nonce[128] = {0};
        if(status_code == 401){
            // Get www-authenticate header (try both cases)
            char header[512] = {0};
            if (pal_http_client_get_header(http_handle, "www-authenticate", header, sizeof(header)) != 0) {
                // Try with capital letters
                pal_http_client_get_header(http_handle, "WWW-Authenticate", header, sizeof(header));
            }
            
            if (header[0] != '\0') {
                LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "WWW-Authenticate header: %s", header);
                // Parse nonce from header
                char* nonce_start = strstr(header, "nonce=\"");
                if (nonce_start != NULL) {
                    nonce_start += 7; // Skip 'nonce="'
                    char* nonce_end = strchr(nonce_start, '"');
                    if (nonce_end != NULL) {
                        size_t nonce_len = nonce_end - nonce_start;
                        if (nonce_len < sizeof(nonce)) {
                            memcpy(nonce, nonce_start, nonce_len);
                            nonce[nonce_len] = '\0';
                            is_nonce_detected = true;
                        }
                    }
                }
            }
            
            if (!is_nonce_detected) {
                LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nonce field not found.");
            }
        }

        pal_http_response_free(&response);
        pal_http_client_cleanup(http_handle);

        if(!is_nonce_detected){
            ret = ERROR_APP_CLOUD_NO_NONCE;
            break;
        }

        // Calculate authentication token
        char token[PAL_SHA256_DIGEST_LENGTH * 2 + 1] = {0};
        _calc_sha256(nonce, mac_address_str_cloud_id, key, token, sizeof(token));

        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Getting agent information...");
        
        // Re-initialize HTTP client for authentication request
        if (pal_http_client_init(&http_config, &http_handle) != 0) {
            LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "Failed to initialize HTTP client");
            ret = ERROR_APP_CLOUD_GET_AGENT_CONFIG_FAILED;
            break;
        }
        
        // Build authentication header
        char authentication[512] = {0};
        snprintf(authentication, sizeof(authentication), 
                "SAS-AC1 nonce=\"%s\" id=\"%s\" token=\"%s\"", 
                nonce, mac_address_str_cloud_id, token);
        
        pal_http_client_set_header(http_handle, "Authorization", authentication);
        
        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Authorization header: %s", authentication);
        
        status_code = pal_http_client_get(http_handle, &response);
        
        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Authenticated request status code: %d", status_code);
        
        char device_id[SIZE_OF_UUID] = {0};
        char secret[SIZE_OF_SECRET] = {0};
        bool is_authentication_resolved = false;
        
        if(status_code == 201 || status_code == 200){

            LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: Response Received");
            JsonDocument root;
            deserializeJson(root, response.body, DeserializationOption::NestingLimit(20));
            if(root.containsKey("data")){
                JsonObject data = root["data"].as<JsonObject>();
                if(data.containsKey("device_id")){
                    const char* dev_id = data["device_id"].as<const char*>();
                    strncpy(device_id, dev_id, sizeof(device_id) - 1);
                    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Device ID = %s", device_id);
                }
                else{
                    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "No device ID found");
                    pal_http_response_free(&response);
                    pal_http_client_cleanup(http_handle);
                    ret = ERROR_APP_CLOUD_GET_AGENT_CONFIG_FAILED;                    
                    break;
                }

                if(data.containsKey("secret")){
                    const char* sec = data["secret"].as<const char*>();
                    strncpy(secret, sec, sizeof(secret) - 1);
                }
                else{
                    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "No secret found");
                    pal_http_response_free(&response);
                    pal_http_client_cleanup(http_handle);
                    ret = ERROR_APP_CLOUD_GET_AGENT_CONFIG_FAILED;
                    break;
                }

                is_authentication_resolved = true;
            }

            // Combine agent ID and token with a colon
            char agent_id_token[SIZE_OF_UUID + SIZE_OF_SECRET + 2] = {0};
            snprintf(agent_id_token, sizeof(agent_id_token), "%s:%s", device_id, secret);

            // Convert to Base64
            char agent_id_token_base64[SIZE_OF_SECRET] = {0};
            pal_crypto_base64_encode((const uint8_t*)agent_id_token, strlen(agent_id_token), 
                                    agent_id_token_base64, sizeof(agent_id_token_base64));
            
            memset(_network_config.agent_uuid, 0, SIZE_OF_UUID);
            memcpy(_network_config.agent_uuid, device_id, strlen(device_id));
            memset(_network_config.basic_authentication_base64, 0, SIZE_OF_SECRET);
            memcpy(_network_config.basic_authentication_base64, agent_id_token_base64, strlen(agent_id_token_base64));
        }
        else{
            ret = ERROR_APP_CLOUD_GET_AGENT_CONFIG_FAILED;
            pal_http_response_free(&response);
            pal_http_client_cleanup(http_handle);
            break;
        }
        
        pal_http_response_free(&response);
        pal_http_client_cleanup(http_handle);

        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Getting Nozzel information...");
        
        // Configure for nozzle config request
        http_config.url = "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/ingress/core/v1/device/config";
        if (pal_http_client_init(&http_config, &http_handle) != 0) {
            LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "Failed to initialize HTTP client");
            ret = ERROR_APP_CLOUD_GET_NOZZLE_CONFIG_FAILED;
            break;
        }
        
        // Build authentication header
        char auth_header[512] = {0};
        snprintf(auth_header, sizeof(auth_header), "Basic %s", _network_config.basic_authentication_base64);
        pal_http_client_set_header(http_handle, "Authorization", auth_header);
        
        status_code = pal_http_client_get(http_handle, &response);
        bool is_nozzle_config_received = false;
        
        if(status_code == 201 || status_code == 200){

            JsonDocument  root;
            deserializeJson(root, response.body, DeserializationOption::NestingLimit(20));

            if(root.containsKey("data")){

                JsonObject data = root["data"].as<JsonObject>();
                JsonArray nozzles = data["nozzles"].as<JsonArray>();
                int no_nozzles = nozzles.size();

                if(no_nozzles > NO_NOZZELS){
                    no_nozzles = NO_NOZZELS;
                }

                is_nozzle_config_received = true;
                for(int i=0; i<no_nozzles; i++){

                    JsonObject nozzle = nozzles[i].as<JsonObject>();
                    const char* device_id_str = nullptr;
                    const char* fuel_type_str_val = nullptr;
                    const char* fuel_type_val = nullptr;
                    const char* nozzle_id_str = nullptr;

                    if(nozzle.containsKey("device_id")){
                        device_id_str = nozzle["device_id"].as<const char*>();
                        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nozzle %d Device ID = %s", i, device_id_str);
                    }
                    else{
                        is_nozzle_config_received = false;
                        break;
                    }

                    if(nozzle.containsKey("fuel_type")){
                        fuel_type_val = nozzle["fuel_type"].as<const char*>();
                        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nozzle %d Fuel Type = %s", i, fuel_type_val);
                    }
                    else{
                        is_nozzle_config_received = false;
                        break;
                    }

                    if(nozzle.containsKey("id")){
                        nozzle_id_str = nozzle["id"].as<const char*>();
                        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nozzle %d Nozzle ID = %s", i, nozzle_id_str);
                    }
                    else{
                        is_nozzle_config_received = false;                        
                        break;
                    }

                    if(nozzle.containsKey("fuel_type_str")){
                        fuel_type_str_val = nozzle["fuel_type_str"].as<const char*>();
                        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nozzle %d Fuel Type String = %s", i, fuel_type_str_val);
                    }
                    else{
                        is_nozzle_config_received = false;
                        break;
                    }

                    strncpy(_nozzle_config[i].uuid, device_id_str, sizeof(_nozzle_config[i].uuid) - 1);
                    strncpy(_nozzle_config[i].fuel_type, fuel_type_val, sizeof(_nozzle_config[i].fuel_type) - 1);
                    strncpy(_nozzle_config[i].fuel_type_str, fuel_type_str_val, sizeof(_nozzle_config[i].fuel_type_str) - 1);
                    strncpy(_nozzle_config[i].nozzle_id, nozzle_id_str, sizeof(_nozzle_config[i].nozzle_id) - 1);
                }
            }
        }
        else{
            ret = ERROR_APP_CLOUD_GET_NOZZLE_CONFIG_FAILED;
            pal_http_response_free(&response);
            pal_http_client_cleanup(http_handle);
            break;
        }
        
        pal_http_response_free(&response);
        pal_http_client_cleanup(http_handle);
        
        if(is_nozzle_config_received){
            LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Configuration load successfull.... " );

            _root_ca = root_ca;

            ret = ERROR_OK;
        }
        else{
            LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Configuration load failed, some parameter is missing.... " );
        }

    }while(false);

    return ret;
}

int32_t cube_sphere_send_hb(heart_beat_info_t hb){

    int32_t ret = ERROR_OK;

    static char strJson[2048];
    memset(strJson, 0, sizeof(strJson));

    do{ 
        JsonDocument jsonBufferPoints;
        JsonObject points = jsonBufferPoints.add<JsonObject>();
        JsonArray array = points["events"].to<JsonArray>(); 
        
        JsonObject root1 = array.add<JsonObject>();
        root1["device"] = _network_config.agent_uuid;
        struct timeval now;
        gettimeofday(&now, NULL);
        
        char time_str[64] = {0};
        _format_iso8601_time(now.tv_sec + 3600*5.5, "+05:30", time_str, sizeof(time_str));
        root1["time"] = time_str;
        
        JsonObject measurements1 = root1.createNestedObject("body");
            measurements1["rssi"] = hb.rssi;
            measurements1["uptime"] = hb.uptime_sec;
        root1["event"] = "core/heartbeat";

        JsonObject root2 = array.createNestedObject();
        root2["device"] = _nozzle_config[0].uuid;
        root2["time"] = time_str;
        JsonObject measurements2 = root2.createNestedObject("body");
            measurements2["rssi"] = hb.rssi;
            measurements2["uptime"] = hb.uptime_sec;
        root2["event"] = "core/heartbeat";

        JsonObject root3 = array.createNestedObject();
        root3["device"] = _nozzle_config[1].uuid;
        root3["time"] = time_str;
        JsonObject measurements3 = root3.createNestedObject("body");
            measurements3["rssi"] = hb.rssi;
            measurements3["uptime"] = hb.uptime_sec;
        root3["event"] = "core/heartbeat";

        serializeJson(points, strJson, sizeof(strJson));

        LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Message : %s", strJson);
        
        ret = cube_sphere_send_event(strJson);
        if(ret == 0){
            LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: HB Success");
        }
        else{
            LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: HB Failed");
        }

    }while(false);

    return ret;

}

int32_t cube_sphere_send_reconnect(reconnect_info_t reconnect){

    int32_t ret = ERROR_OK;
    
    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: Reconnect Event");

    static char strJson[2048];
    memset(strJson, 0, sizeof(strJson));

    JsonDocument jsonBufferPoints;
    JsonObject points = jsonBufferPoints.add<JsonObject>();
    JsonArray array = points["events"].to<JsonArray>();     
    JsonObject root1 = array.add<JsonObject>();    

    root1["device"] = _network_config.agent_uuid;
    struct timeval now;
    gettimeofday(&now, NULL);
    
    char time_str[64] = {0};
    _format_iso8601_time(now.tv_sec + 3600*5.5, "+05:30", time_str, sizeof(time_str));
    root1["time"] = time_str;
    
    JsonObject measurements1 = root1.createNestedObject("body");
        measurements1["rssi"] = reconnect.rssi;
        measurements1["uptime"] = reconnect.uptime_sec;
        measurements1["local_ip"] = reconnect.ip_address;
        measurements1["wifi_ssid"] = reconnect.ssid;
        measurements1["wifi_password"] = reconnect.password;
    root1["event"] = "core/reconnect";

    serializeJson(points, strJson, sizeof(strJson));
    
    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Message : %s", strJson);

    ret = cube_sphere_send_event(strJson);
    if(ret != ERROR_OK)
    {
        LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "FERP-Cloud: Reconnect Failed");
    }

    return ERROR_OK;
}

int32_t cube_sphere_send_pumped(pumped_event_info_t nozzle_event){

    int32_t ret = ERROR_OK;

    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: Sending Pumped Event");

    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "NE : IDX = %d, \
        TS = %llu, U = %lu, \
        P = %llu, V = %llu, EID = %lu",
        nozzle_event.n_idx, nozzle_event.time_stamp,
        nozzle_event.unit_pricex100, nozzle_event.total_pricex100, 
        nozzle_event.volume_lx1000, nozzle_event.event_id
    );

    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nozzle UUID : %s", _nozzle_config[nozzle_event.n_idx].uuid);
    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nozzle Fuel Type : %s", _nozzle_config[nozzle_event.n_idx].fuel_type);
    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nozzle Fuel Type String : %s", _nozzle_config[nozzle_event.n_idx].fuel_type_str);
    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Nozzle Event ID : %s", _nozzle_config[nozzle_event.n_idx].nozzle_id);

    static char strJson[2048];
    memset(strJson, 0, sizeof(strJson));

    JsonDocument  jsonBufferPoints;
    JsonObject points = jsonBufferPoints.add<JsonObject>();
    JsonArray array = points["events"].to<JsonArray>();
    JsonObject root = array.add<JsonObject>();

    root["device"] = _nozzle_config[nozzle_event.n_idx].uuid;
    struct timeval now;
    gettimeofday(&now, NULL);
    
    char time_str[64] = {0};
    _format_iso8601_time(now.tv_sec + 3600*5.5, "+05:30", time_str, sizeof(time_str));
    root["time"] = time_str;

    JsonObject measurements = root.createNestedObject("body");            
        measurements["L"] = nozzle_event.volume_lx1000*0.001;
        measurements["T"] = _nozzle_config[nozzle_event.n_idx].fuel_type;
        measurements["P"] = nozzle_event.total_pricex100*0.01;
        measurements["U"] = nozzle_event.unit_pricex100*0.01;
        measurements["ID"] = nozzle_event.event_id;
    root["event"] = "app.fuel/pump-end";
    serializeJson(points, strJson, sizeof(strJson));

    LOG_MSG_DEBUG(LOG_EN, "Message : %s", strJson);

    ret = cube_sphere_send_event(strJson);
    if(ret != ERROR_OK)
    {
        LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "FERP-Cloud: Pumped Event Failed");
    }
    else
    {
        LOG_MSG_DEBUG(LOG_EN, "FERP-Cloud: Pumped Event Success");
    }

    return ret;
}

int32_t cube_sphere_send_status_updated(startup_info_t startup){

    int32_t ret = ERROR_OK;  

    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: Sending Status Updated Event");

    static char strJson[2048];
    memset(strJson, 0, sizeof(strJson));

    JsonDocument jsonBufferPoints;
    JsonObject points = jsonBufferPoints.add<JsonObject>();
    JsonArray array = points["events"].to<JsonArray>();     
    JsonObject root1 = array.add<JsonObject>();    

    root1["device"] = _network_config.agent_uuid;
    struct timeval now;
    gettimeofday(&now, NULL);
    
    char time_str[64] = {0};
    _format_iso8601_time(now.tv_sec + 3600*5.5, "+05:30", time_str, sizeof(time_str));
    root1["time"] = time_str;
    
    JsonObject measurements1 = root1.createNestedObject("body");
        measurements1["hw_type"] = startup.device_type;
        measurements1["hw_version"] = startup.board_version;
        measurements1["sw_version"] = startup.fw_version;
        measurements1["local_ip"] = startup.ip_address;
        measurements1["mac"] = startup.mac_address_str;
        measurements1["wifi_ssid"] = startup.ssid;
        measurements1["wifi_password"] = startup.password;
        measurements1["dt_version"] = startup.esp07_fw_version;
        measurements1["sd_status"] = startup.sd_card_status;
        measurements1["sd_size"] = startup.sd_card_size_str;
    root1["event"] = "core/status-updated";

    serializeJson(points, strJson, sizeof(strJson));
    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Message : %s", strJson);

    ret = cube_sphere_send_event(strJson);
    if(ret != ERROR_OK)
    {
        LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "FERP-Cloud: Status Update Failed");
    }

    return ERROR_OK;
}

int32_t cube_sphere_send_startup(startup_info_t startup){

    int32_t ret = ERROR_OK;  
    
    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "FERP-Cloud: Sending Startup Event");

    static char strJson[2048];
    memset(strJson, 0, sizeof(strJson));

    JsonDocument jsonBufferPoints;
    JsonObject points = jsonBufferPoints.add<JsonObject>();
    JsonArray array = points["events"].to<JsonArray>();     
    JsonObject root1 = array.add<JsonObject>();    

    root1["device"] = _network_config.agent_uuid;
    struct timeval now;
    gettimeofday(&now, NULL);
    
    char time_str[64] = {0};
    _format_iso8601_time(now.tv_sec + 3600*5.5, "+05:30", time_str, sizeof(time_str));
    root1["time"] = time_str;
    
    JsonObject measurements1 = root1.createNestedObject("body");
        measurements1["hw_type"] = startup.device_type;
        measurements1["hw_version"] = startup.board_version;
        measurements1["sw_version"] = startup.fw_version;
        measurements1["local_ip"] = startup.ip_address;
        measurements1["mac"] = startup.mac_address_str;
        measurements1["wifi_ssid"] = startup.ssid;
        measurements1["wifi_password"] = startup.password;
        measurements1["dt_version"] = startup.esp07_fw_version;
        measurements1["sd_status"] = startup.sd_card_status;
        measurements1["sd_size"] = startup.sd_card_size_str;
    root1["event"] = "core/startup";

    serializeJson(points, strJson, sizeof(strJson));
    LOG_MSG_DEBUG(CSP_DEBUG_LOG_EN, "Message : %s", strJson);

    ret = cube_sphere_send_event(strJson);
    if(ret != ERROR_OK)
    {
        LOG_MSG_ERROR(CSP_DEBUG_LOG_EN, "FERP-Cloud: Startup Failed");
    }

    return ERROR_OK;
}

int32_t cube_sphere_send_printed(pumped_event_info_t startup){

    int32_t ret = ERROR_OK;

    return ERROR_OK;
}

int32_t cube_sphere_get_device_uuid(char* device_uuid, uint32_t len){

    if(len < SIZE_OF_UUID){
        return -1;
    }

    memset(device_uuid, 0, len);
    memcpy(device_uuid, _network_config.agent_uuid, SIZE_OF_UUID);

    return 0;
}


