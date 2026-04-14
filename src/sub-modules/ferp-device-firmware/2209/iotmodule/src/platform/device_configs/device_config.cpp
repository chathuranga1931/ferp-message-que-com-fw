

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include "device_config.h"
#include "logger.h"
#include "error.h"
#include "app.h"

#if defined(FERP_COM)
#define NO_CONFIGS					(9)
#elif defined(FERP_PRINTER)
#define NO_CONFIGS					(2)
#endif

#define FN_DEVICE_CONFIGURATIONS 	"DeviceConfigs.json"
#define FN_DEVICE_SETTINGS 			"DeviceSettings.json"
#define FN_NETWORK_CONFIGURATIONS 	"NetworkSettings.json"
#define DIR_CONFIGURATION			"Configs"

device_configs_t * _device_configs = nullptr;
AsyncWebServer * _server = nullptr;

typedef enum {
	config_value_type_character = 0,
	config_value_type_integer,
	config_value_type_string,
}config_value_type_t;

typedef struct{
	String name;
	config_value_type_t type;
	void * p_global_value;
	uint16_t max_length;
}config_t;

config_t _tbl_configs[NO_CONFIGS];

/* For reference */
// void changePage_DeviceConfigurations(AsyncWebServerRequest *request){

// 	logger.log("changePage_DeviceConfigurations");
// 	request->send(SPIFFS, "/deviceConfigurations.html", String(), false, nullptr);
// }

void saveDeviceConfigurations(){

	logger.log("saveDeviceConfigurations");

	String configFolder = String(DIR_CONFIGURATION);
	String filename = String(FN_DEVICE_CONFIGURATIONS);
	String filePath = "/" + configFolder + "/" + filename;
	if(!filePath.startsWith("/"))
		filePath = "/"+ filePath;

	// delete the existing file
	if(SPIFFS.exists(filePath)){
		SPIFFS.remove(filePath);
		logger.log("file Removed");
	}
	else{
		logger.log("no file");
	}

	logger.log("File Path: " + filePath);
	File deviceConfigurationsFile = SPIFFS.open(filePath, "w");

	StaticJsonBuffer<1024> jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();

	for(int i=0; i<NO_CONFIGS; i++){
		switch (_tbl_configs[i].type)
		{
			case config_value_type_string:
				root[_tbl_configs[i].name] = (char *)(_tbl_configs[i].p_global_value);
				logger.log(_tbl_configs[i].name + " : " + String((char *)(_tbl_configs[i].p_global_value)));
			break;
			case config_value_type_integer:

			break;
		default:
			break;
		}

	}

  	root.printTo(deviceConfigurationsFile); // Exporte et enregsitre le JSON dans la zone SPIFFS - Export and save JSON object to SPIFFS area
#if LOG_ENABLED
	root.printTo(LOGGER_UART);
#endif

  	deviceConfigurationsFile.close();
}

ret_t load_defult_configurations(){

	// _tbl_configs[0] = { "ssid",         config_value_type_string, _device_configs->wifi.ssid,                  SIZE_OF_WIFI_SSID    };
	// _tbl_configs[1] = { "password",     config_value_type_string, _device_configs->wifi.password,              SIZE_OF_WIFI_PASSWORD};
	// _tbl_configs[2] = { "cloud_url",    config_value_type_string, _device_configs->network.url, 		       SIZE_OF_NTWK_BASE_URL};
	// _tbl_configs[3] = { "cloud_secret", config_value_type_string, _device_configs->network.secret,             SIZE_OF_SECRET};
	// _tbl_configs[4] = { "uuid",         config_value_type_string, _device_configs->UUID,    	     	       SIZE_OF_UUID};
	// _tbl_configs[5] = { "nozzel_id",    config_value_type_string, _device_configs->nozel_configs.nozzel_id,    SIZE_OF_NOZZELID};
	// _tbl_configs[6] = { "fuel_type",    config_value_type_string, _device_configs->nozel_configs.fuel_type,    SIZE_OF_FUEL_TYPE};
	// _tbl_configs[7] = { "printer_url",  config_value_type_string, _device_configs->printer.url, 

	strcpy(_device_configs->wifi.ssid, DEFAULT_WIFI_SSID);
	strcpy(_device_configs->wifi.password, DEFAULT_WIFI_PW);
#if defined(FERP_COM)
	strcpy(_device_configs->UUID, DEFAULT_UUID);
	strcpy(_device_configs->network.url, DEFAULT_CLOUD_URL);
	strcpy(_device_configs->network.secret, DEFAULT_CLOUD_SECRET);
	strcpy(_device_configs->nozel_configs.nozzel_id, DEFAULT_NOZZEL_ID);
	strcpy(_device_configs->nozel_configs.fuel_type, DEFAULT_FUEL_TYPE);
	strcpy(_device_configs->nozel_configs.fuel_type_str, DEFAULT_FUEL_TYPE_STR);
	strcpy(_device_configs->printer.url, DEFAULT_PRINTER_URL);
#elif defined(FERP_PRINTER)
#endif
	
	saveDeviceConfigurations();
}

ret_t loadDeviceConfigurations(){

	ret_t ret = ret_Success;

	do{
		String configFolder = String(DIR_CONFIGURATION);
		String filename = String(FN_DEVICE_CONFIGURATIONS);
		String filePath = "/" + configFolder + "/" + filename;
		if(!filePath.startsWith("/"))
			filePath = "/" + filePath;


		logger.log("File Path: " + filePath);
		if(!SPIFFS.exists(filePath)){
			logger.log("no device configuration file, Use default configuraitons");
			_device_configs->status.config_file_status = config_file_not_available;
			load_defult_configurations();
			break;
		}

		File deviceConfigurationsFile = SPIFFS.open(filePath, "r");
		String fileContent = deviceConfigurationsFile.readString();

		logger.log(fileContent);

		//DynamicJsonBuffer use this if any issue came with size of the buffers
		StaticJsonBuffer<1024> jsonBuffer;
		JsonObject& root = jsonBuffer.parseObject(fileContent);

		for(int i=0; i<NO_CONFIGS; i++){
			switch (_tbl_configs[i].type)
			{
				case config_value_type_string:
					if(root.containsKey(_tbl_configs[i].name)){
						String str_value = root[_tbl_configs[i].name].as<String>();
						memset( (char *)(_tbl_configs[i].p_global_value) , 0, _tbl_configs[i].max_length);
						str_value.toCharArray((char *)(_tbl_configs[i].p_global_value), _tbl_configs[i].max_length, 0);
						logger.log(_tbl_configs[i].name + " : " + String((char *)(_tbl_configs[i].p_global_value)));
					}
				break;
				case config_value_type_integer:
					LOG_FDETAILS("TODO: Implement config_value_type_integer case");
				break;
			default:
				break;
			}
		}

		deviceConfigurationsFile.close();

	}while(false);

	return ret;
}

// void saveNetworkConfigurations(){

// 	logger.log("saveNetworkConfigurations");

// 	String configFolder = String(DIR_CONFIGURATION);
// 	String filename = String(FN_NETWORK_CONFIGURATIONS);
// 	String filePath = "/" + configFolder + "/" + filename;
// 	if(!filePath.startsWith("/"))
// 		filePath = "/"+filePath;

// 	// delete the existing file
// 	if(SPIFFS.exists(FN_NETWORK_CONFIGURATIONS)){
// 		SPIFFS.remove(filePath);
// 		logger.log("file Removed");
// 	}
// 	else{
// 		logger.log("no file");
// 	}

// 	File settingsFile = SPIFFS.open(filePath, "w");

// 	StaticJsonBuffer<100> jsonBuffer;
// 	JsonObject& root = jsonBuffer.createObject();

// 	root["base_url"] = _device_configs->network.url;
// 	root["auth_key"] = _device_configs->network.secret;

//   	root.printTo(settingsFile); // Exporte et enregsitre le JSON dans la zone SPIFFS - Export and save JSON object to SPIFFS area
// 	root.printTo(Serial);
// 	logger.log(" ");

// 	logger.log("base_url : "); logger.log(_device_configs->network.url);
// 	logger.log("auth_key : "); logger.log(_device_configs->network.secret);

//   	settingsFile.close();
// }

// void loadNetworkConfigurations(){

// 	do{
// 		String configFolder = String(DIR_CONFIGURATION);
// 		String filename = String(FN_NETWORK_CONFIGURATIONS);
// 		String filePath = "/" + configFolder + "/" + filename;
// 		if(!filePath.startsWith("/"))
// 			filePath = "/"+filePath;

// 		// delete the existing file
// 		if(SPIFFS.exists(FN_NETWORK_CONFIGURATIONS)){
// 			logger.log("no FN_NETWORK_CONFIGURATIONS file");
// 			break;
// 		}

// 		File settingsFile = SPIFFS.open(filePath, "r");
// 		String fileContent = settingsFile.readString();

// 		logger.log(fileContent);

// 		//DynamicJsonBuffer use this if any issue came with size of the buffers
// 		StaticJsonBuffer<500> jsonBuffer;
// 		JsonObject& root = jsonBuffer.parseObject(fileContent);

// 		// if(root.containsKey("base_url")){
// 		// 	logger.log("base_url available");
// 		// 	String element = root["base_url"].as<String>();
// 		// 	memset( _device_configs->network.url , 0, SIZE_OF_NTWK_BASE_URL);
// 		// 	element.toCharArray(_device_configs->network.url, SIZE_OF_NTWK_BASE_URL, 0);
// 		// }
// 		// if(root.containsKey("auth_key")){
// 		// 	logger.log("auth_key available");
// 		// 	String element = root["auth_key"].as<String>();
// 		// 	memset( _device_configs->network.secret.c_str() , 0, SIZE_OF_NTWK_ACCESS_TOKEN);
// 		// 	element.toCharArray(_device_configs->network.secret, SIZE_OF_NTWK_ACCESS_TOKEN, 0);
// 		// }

// 		settingsFile.close();

// 		logger.log("base_url : "); logger.log(_device_configs->network.url);
// 		logger.log("auth_key : "); logger.log(_device_configs->network.secret);

// 	}while(false);
// }

void handleGetDeviceInformation(AsyncWebServerRequest *request){

	logger.log("handleGetDeviceInformation");

	StaticJsonBuffer<1024> jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();
	
	root["ssid"] = String(_device_configs->wifi.ssid);
	root["ipaddress"] = String(_device_configs->ip_address);
#if defined(FERP_COM)
	root["device_version"] = "FERP-IoT-Com (" + String(FW_VERSION) + ")";
	root["displayvalue"] = String(_device_configs->display_value_last_str);
#elif defined(FERP_PRINTER)
	root["device_version"] = "FERP-IoT-Printer (" + String(FW_VERSION) + ")";
	root["displayvalue"] = "N/A";
#endif

	// for(int i=0; i<NO_CONFIGS; i++){
	// 	switch (_tbl_configs[i].type)
	// 	{
	// 		case config_value_type_string:
	// 			root[_tbl_configs[i].name] = (char *)(_tbl_configs[i].p_global_value);
	// 			logger.log(_tbl_configs[i].name + " : " + String((char *)(_tbl_configs[i].p_global_value)));
	// 		break;
	// 		case config_value_type_integer:

	// 		break;
	// 	default:
	// 		break;
	// 	}
	// }

	// logger.log(String(_device_configs->wifi.ssid));
	// logger.log(String(_device_configs->wifi.password));
	root.printTo(Serial);
	logger.log(" ");

	String jsonStr;
	root.printTo(jsonStr);
	request->send(200, "application/json", jsonStr);
}


void handleGetDeviceConfigurations(AsyncWebServerRequest *request){

	logger.log("handleGetDeviceConfigurations");

	StaticJsonBuffer<1024> jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();
	
	// root["ssid"] = String(_device_configs->wifi.ssid);
	// root["password"] = String(_device_configs->wifi.password);

	for(int i=0; i<NO_CONFIGS; i++){
		switch (_tbl_configs[i].type)
		{
			case config_value_type_string:
				root[_tbl_configs[i].name] = (char *)(_tbl_configs[i].p_global_value);
				logger.log(_tbl_configs[i].name + " : " + String((char *)(_tbl_configs[i].p_global_value)));
			break;
			case config_value_type_integer:

			break;
		default:
			break;
		}
	}

	// logger.log(String(_device_configs->wifi.ssid));
	// logger.log(String(_device_configs->wifi.password));
	root.printTo(Serial);
	logger.log(" ");

	String jsonStr;
	root.printTo(jsonStr);
	request->send(200, "application/json", jsonStr);
}

void handleSetDeviceConfigurationsPost(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {

    static String strdata = "";

    if(index==0){
        strdata = "";
    }

    strdata = strdata + String(data, len);

    if(index + len >= total){
        logger.log(strdata);

		if(total > 512){
			logger.log("Err.. Insuffinet buffer at device config");
			return;
		}

		StaticJsonBuffer<1024> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(strdata);
        if(root.containsKey("ssid")){
            String value = "";
			for(int i=0; i<NO_CONFIGS; i++){
				switch(_tbl_configs[i].type){
					case config_value_type_character:
					break;
					case config_value_type_integer:
					break;
					case config_value_type_string:
						value = root[_tbl_configs[i].name].as<String>();
						value.toCharArray((char *)(_tbl_configs[i].p_global_value), _tbl_configs[i].max_length);
						// logger.log("global pointer = " + String((uint32_t)(_tbl_configs[i].p_global_value)));
						logger.log(_tbl_configs[i].name + " : " + value);
						// logger.log("g_device.password = " + String(_device_configs->wifi.password));
					break;
					default:
					break;
				}
			}
		}
	}

	saveDeviceConfigurations();
}

void handleSetDeviceConfigurations(AsyncWebServerRequest *request){

	logger.log("handleSetDeviceConfigurations");

	int params = request->args();
	logger.log(String(params));

	if(params != 2){
		request->send(200, "text/plain", "Set configuration failed");
	}
	else{

		for(int i=0; i<NO_CONFIGS; i++){
			switch(_tbl_configs[i].type){
				case config_value_type_character:
				break;
				case config_value_type_integer:
				break;
				case config_value_type_string:
					request->arg(_tbl_configs[i].name).toCharArray((char *)(_tbl_configs[i].p_global_value), _tbl_configs[i].max_length);
					// logger.log("global pointer = " + String((uint32_t)(_tbl_configs[i].p_global_value)));
					// logger.log(request->arg(_tbl_configs[i].name));
					// logger.log("g_device.ssid = " + String(_device_configs->wifi.ssid));
					// logger.log("g_device.password = " + String(_device_configs->wifi.password));
				break;
				default:
				break;
			}
		}
		request->send(200, "text/plain", "Set configuration success");
	}

	saveDeviceConfigurations();
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  Serial.println(logmessage);

  if (!index) {
    logmessage = "Upload Start: " + String(filename);
    // open the file on first call and store the file handle in the request object
    request->_tempFile = SPIFFS.open("/" + filename, "w");
    Serial.println(logmessage);
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    Serial.println(logmessage);
  }

  if (final) {
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    // close the file handle as the upload is now done
    request->_tempFile.close();
    Serial.println(logmessage);
    request->redirect("/");
  }
}

// void handleFileList()
// {
// //   String path = "/";
// //   // Assuming there are no subdirectories
// //   Dir dir = SPIFFS.openDir(path);

// //   String output = "[";
// //   while(dir.next())
// //   {
// //     File entry = dir.openFile("r");
// //     // Separate by comma if there are multiple files
// //     if(output != "[")
// //       output += ",";
// //     output += String(entry.name()).substring(1);
// //     entry.close();
// //   }
// //   output += "]";
// //   server.send(200, "text/plain", output);

// 	File root = SPIFFS.open("/");

// 	File file = root.openNextFile();

// 	String output = "[";
// 	while(file){

// 		// Separate by comma if there are multiple files
// 		if(output != "[")
// 		output += ",";
// 		output += String(file.name());

// 		Serial.print("FILE: ");
// 		Serial.println(file.name());
// 		file.close();
// 		file = root.openNextFile();
// 	}
// 	output += "]";
// 	server.send(200, "text/plain", output);
// }

ret_t device_config_start_server(){

	ret_t ret = ret_Success;

	do{
		if(!_device_configs){
			LOG_FDETAILS("Not Initialized...");
			ret = ret_Err_Gen;
			break;
		}

		_server->on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
			request->send(SPIFFS, "/styles.css", "text/css");
		});

		_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
			logger.log("Index Page");
			request->send(SPIFFS, "/index.html", String(), false, nullptr);
		});

		_server->on("/contact", HTTP_GET, [](AsyncWebServerRequest *request){
			logger.log("Contact Page");
			request->send(SPIFFS, "/contact.html", String(), false, nullptr);
		});

		_server->on("/about", HTTP_GET, [](AsyncWebServerRequest *request){
			logger.log("About Page");
			request->send(SPIFFS, "/about.html", String(), false, nullptr);
		});

		// _server->on("/deviceConfigurations.html", changePage_DeviceConfigurations);
#if defined(FERP_COM)
		_server->on("/deviceConfigurations", HTTP_GET, [](AsyncWebServerRequest *request){
			logger.log("changePage_DeviceConfigurations");
			request->send(SPIFFS, "/deviceConfigurations.html", String(), false, nullptr);
		});
#elif defined(FERP_PRINTER)
		_server->on("/deviceConfigurations", HTTP_GET, [](AsyncWebServerRequest *request){
			logger.log("changePage_DeviceConfigurations");
			request->send(SPIFFS, "/deviceConfPrinter.html", String(), false, nullptr);
		});
#endif


		_server->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
			request->send(200);
		}, handleUpload);


		// server.on("/uploading", HTTP_POST, handleUploading);
		// server.on("/getNetworkConfigurations", HTTP_GET, handleGetNetworkConfigurations);
		// server.on("/setNetworkCo

		_server->on("/getDeviceConfigurations", HTTP_GET, handleGetDeviceConfigurations);
		_server->on("/setDeviceConfigurations", HTTP_GET, handleSetDeviceConfigurations);
		_server->on("/getDeviceInformation", HTTP_GET, handleGetDeviceInformation);

		_server->on("/setDeviceConfigurationsPost", HTTP_POST, [](AsyncWebServerRequest *request) {
            logger.log("setDeviceConfigurationsPost");
			request->send(200);
        }, nullptr, handleSetDeviceConfigurationsPost);

	}while(false);

	return ret;
}

// ret_t loadDefaultDeviceConfigurations(){
// 	/* Implement code here */

// 	ret_t ret = ret_Success;
// 	LOG_FDETAILS("TODO: Load default device configurations");
// 	return ret;
// }

ret_t device_config_init(device_configs_t * device_configs){

	ret_t ret = ret_Success;

    do{
        if(device_configs != nullptr){
            _device_configs = device_configs;
        }
        else{
            logger.log("[" + String(__FILENAME__) + "]" + String(__LINE__));
			ret = ret_Err_Gen_NullP;
			break;
        }

        if(device_configs->async_server != nullptr){
            _server = device_configs->async_server;
        }
        else{
            logger.log("[" + String(__FILENAME__) + "]" + String(__LINE__));
			ret = ret_Err_Gen_NullP;
			break;
        }

		// ret = loadDefaultDeviceConfigurations();
		// load_defult_configurations();

#if defined(FERP_COM)
		_tbl_configs[0] = { "ssid",         config_value_type_string, _device_configs->wifi.ssid,                  SIZE_OF_WIFI_SSID    };
		_tbl_configs[1] = { "password",     config_value_type_string, _device_configs->wifi.password,              SIZE_OF_WIFI_PASSWORD};
		_tbl_configs[2] = { "cloud_url",    config_value_type_string, _device_configs->network.url, 		       SIZE_OF_NTWK_BASE_URL};
		_tbl_configs[3] = { "cloud_secret", config_value_type_string, _device_configs->network.secret,             SIZE_OF_SECRET};
		_tbl_configs[4] = { "uuid",         config_value_type_string, _device_configs->UUID,    	     	       SIZE_OF_UUID};
		_tbl_configs[5] = { "nozzel_id",    config_value_type_string, _device_configs->nozel_configs.nozzel_id,    SIZE_OF_NOZZELID};
		_tbl_configs[6] = { "fuel_type",    config_value_type_string, _device_configs->nozel_configs.fuel_type,    SIZE_OF_FUEL_TYPE};
		_tbl_configs[7] = { "fuel_type_str",config_value_type_string, _device_configs->nozel_configs.fuel_type_str,SIZE_OF_FUEL_TYPE_STR};
		_tbl_configs[8] = { "printer_url",  config_value_type_string, _device_configs->printer.url, 			   SIZE_OF_NTWK_BASE_URL};
#elif defined(FERP_PRINTER)
		_tbl_configs[0] = { "ssid",         config_value_type_string, _device_configs->wifi.ssid,                  SIZE_OF_WIFI_SSID    };
		_tbl_configs[1] = { "password",     config_value_type_string, _device_configs->wifi.password,              SIZE_OF_WIFI_PASSWORD};
#endif
		ret = loadDeviceConfigurations();

    }while(false);

	return ret;
}