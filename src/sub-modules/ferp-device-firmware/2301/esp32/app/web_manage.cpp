
/*==============================================================================

   INCLUDES

==============================================================================*/
#include "web_manage.h"
#include <JsonObjectStream.h>
#include <esp_wifi.h>
#include "Network/Mdns/Responder.h"
#include "Network/Mdns/debug.h"
#include "syssettings.h"
#include "server_com.h"

/*==============================================================================

   DEFINES

==============================================================================*/
// #define CAPTIVE_PORTAL_ENABLED // comment to disable

/*==============================================================================

   TYPES

==============================================================================*/

/*==============================================================================

   NATIONAL FUNCTION DECLARATIONS

==============================================================================*/

class MyHttpService : public mDNS::Service
{
public:
    String getInstance() override
    {
        return SysSettings.HostName.c_str(); // F("UDP Server");
    }
};

/*==============================================================================

   GLOBAL VARIABLE DEFINITIONS

==============================================================================*/

/*==============================================================================

   NATIONAL VARIABLE DEFINITIONS

==============================================================================*/
Timer TimerChkWiFi;

HttpServer server;
FtpServer ftp;
DnsServer DNS;

mDNS::Responder responder;
MyHttpService myHttpService;

/*==============================================================================

   GLOBAL FUNCTION DEFINITIONS

==============================================================================*/

/*==============================================================================

   GLOBAL FUNCTION DEFINITIONS

==============================================================================*/
/*------------------------------------------------------------------------------
DESCRIPTION:
check for the captive portal request and send the file
 reference links
 https://stackoverflow.com/questions/54583818/esp-auto-login-accept-message-by-os-with-redirect-to-page-like-public-wifi-port
 https://stackoverflow.com/questions/54905328/passing-captive-portal-with-esp8266
 https://stackoverflow.com/questions/55138577/how-to-redirect-to-default-browser-from-captive-portal-cna-browser

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
bool WM_captivePortal(HttpRequest &request, HttpResponse &response)
{
    // return false; // to ignore captive portal
    String location = request.headers[HTTP_HEADER_HOST];
    String file = request.uri.getRelativePath();
    debug_i("captive location = %s, file = %s\r\n", location.c_str(), file.c_str());

    // automatic redirect and request to open captive portal web page to open on external browser because captive portal is not support external files
    if (location == WM_capUrlAndroid || location == WM_capUrlAndroidold || location == WM_capUrlFireFox || location == WM_capUrlMicrosoft)
    {
        response.headers[HTTP_HEADER_LOCATION] = "intent://" + SysSettings.HostName + "#Intent;scheme=http;end";
        response.code = HTTP_STATUS_FOUND;
        response.setCache(86400, true); // It's important to use cache for better performance.
        // debug_i("redirect... location=%s   newloc=%s\r\n", location.c_str(), response.headers[HTTP_HEADER_LOCATION].c_str());
        // debug_i("\r\n%s\r\n", request.toString().c_str());
        return true;
    }
    // else if(location != WM_HostName)
    else if (location != SysSettings.HostName)
    {
        response.headers[HTTP_HEADER_LOCATION] = "http://" + SysSettings.HostName;
        response.code = HTTP_STATUS_FOUND;
        response.setCache(86400, true); // It's important to use cache for better performance.
        return true;
    }
    return false;
}
/*------------------------------------------------------------------------------
DESCRIPTION:
 handling captive portal.
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_onHandleRoot(HttpRequest &request, HttpResponse &response)
{
#ifdef CAPTIVE_PORTAL_ENABLED
    // if not on request from desired hostname, redirect the page
    if (WM_captivePortal(request, response))
    {
        return;
    }
#endif
    debug_i("On Root");
    response.sendFile("index.html");
}

/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_onFileAP(HttpRequest &request, HttpResponse &response)
{
#ifdef CAPTIVE_PORTAL_ENABLED
    // if not on request from desired hostname, redirect the page
    if (WM_captivePortal(request, response))
    {
        return;
    }
#endif

    String location = request.headers[HTTP_HEADER_HOST];
    String file = request.uri.getRelativePath();
    // debug_i("file path = %s, location = %s\r\n", file.c_str(), location.c_str());
    if (file[0] == '.')
    {
        response.code = HTTP_STATUS_FORBIDDEN;
    }
    else
    {
        response.setCache(86400, true); // It's important to use cache for better performance.
        response.sendFile(file);
    }
}
/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_onWiFiSettings(HttpRequest &request, HttpResponse &response)
{
    JsonObjectStream *stream = new JsonObjectStream();
    JsonObject json = stream->getRoot();

    json["status"] = (bool)true;

    if (request.method == HTTP_POST)
    {
        if (request.getPostParameter("request") == "data")
        {
            debug_i("Sys Sett Req");
            json["ssid"] = SysSettings.STAssid.c_str();
            json["password"] = SysSettings.STApass.c_str();

            json["result"] = "success";
            // response.code = HTTP_STATUS_OK;
            
        }
        else if (request.getPostParameter("request") == "save")
        {
            debug_i("Sys Sett Save");
            if(request.getPostParameter("ssid")){
                SysSettings.STAssid = request.getPostParameter("ssid");
                SysSettings.STApass = request.getPostParameter("password");

                System.queueCallback(TaskDelegate([](){ SysSettings.save(); }));
                json["result"] = "success";
                // response.code = HTTP_STATUS_OK;
            }
            else
            {
                json["result"] = "unsuccess";
                // response.code = HTTP_STATUS_NO_CONTENT;
            }
        }
        else if (request.getPostParameter("request") == "stamode")
        {
            debug_i("Station mode start request");
            System.queueCallback(TaskDelegate([](){ SysSettings.STAmode = true; SysSettings.save(); System.restart(2000); }));
            json["result"] = "success";
        }
        else{
            debug_i("Unknow SysSettings Rquest");
            json["result"] = "unsuccess";
            // response.code = HTTP_STATUS_NO_CONTENT;
        }
    }

    response.setAllowCrossDomainOrigin("*");
    response.sendDataStream(stream, MIME_JSON);
}
/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_onServerSettings(HttpRequest &request, HttpResponse &response)
{
    JsonObjectStream *stream = new JsonObjectStream();
    JsonObject json = stream->getRoot();

    json["status"] = (bool)true;

    if (request.method == HTTP_POST)
    {
        if (request.getPostParameter("request") == "data")
        {
            debug_i("Sys Sett Req");
            json["server"] = SysSettings.Server.c_str();
            json["port"] = (uint16_t)SysSettings.Port;
            json["id_module"] = SysSettings.ID_Module.c_str();
            json["id_nozzel_1"] = SysSettings.ID_Nozzel_1.c_str();
            json["fueltype_nozzel_1"] = (uint8_t)SysSettings.FuelType_Nozzel_1;
            json["id_nozzel_2"] = SysSettings.ID_Nozzel_1.c_str();
            json["fueltype_nozzel_2"] = (uint8_t)SysSettings.FuelType_Nozzel_1;

            json["result"] = "success";
            // response.code = HTTP_STATUS_OK;
            
        }
        else if (request.getPostParameter("request") == "save")
        {
            debug_i("Sys Sett Save");
            if(request.getPostParameter("server")){
                SysSettings.Server = request.getPostParameter("");
                SysSettings.Port = request.getPostParameter("port").toInt();
                SysSettings.ID_Module = request.getPostParameter("id_module");
                SysSettings.ID_Nozzel_1 = request.getPostParameter("id_nozzel_1");
                SysSettings.FuelType_Nozzel_1 = (FUELteType)request.getPostParameter("fueltype_nozzel_1").toInt();
                SysSettings.ID_Nozzel_1 = request.getPostParameter("id_nozzel_2");
                SysSettings.FuelType_Nozzel_1 = (FUELteType)request.getPostParameter("fueltype_nozzel_2").toInt();

                System.queueCallback(TaskDelegate([](){ SysSettings.save(); }));
                json["result"] = "success";
                // response.code = HTTP_STATUS_OK;
            }
            else
            {
                json["result"] = "unsuccess";
                // response.code = HTTP_STATUS_NO_CONTENT;
            }
        }
        // else if (request.getPostParameter("request") == "stamode")
        // {
        //     debug_i("Station mode start request");
        //     System.queueCallback(TaskDelegate([](){ SysSettings.STAmode = true; SysSettings.save(); System.restart(2000); }));
        //     json["result"] = "success";
        // }
        else{
            debug_i("Unknow SysSettings Rquest");
            json["result"] = "unsuccess";
            // response.code = HTTP_STATUS_NO_CONTENT;
        }
    }

    response.setAllowCrossDomainOrigin("*");
    response.sendDataStream(stream, MIME_JSON);
}

/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_onSTAdisconnect(const String &ssid, MacAddress bssid, WifiDisconnectReason reason)
{                                                                                                                                                // WifiStation.getConnectionStatus();
    debug_i("Disconnected STA = %d, %s", WifiStation.getConnectionStatus(), WifiEvents.getDisconnectReasonName(reason).c_str()); // WifiStation.getConnectionStatusName().c_str());
    if (!SysSettings.STAmode)
    {
        TimerChkWiFi.stop();
        WifiStation.connect();
    }

}
/*------------------------------------------------------------------------------
DESCRIPTION:
 Check wifi connectivity. If failed, restart. If wifi diconnect event occured,
 this disables. This enables on WiFi got IP
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_chkWiFi()
{
    if (WifiStation.isConnected())
    {
        debug_i("WiFi OK");
    }
    else
    {
        debug_i("WiFi Disconnected. Restarting...");
        system_restart();
    }
}

void WM_onWiFiDisconnect()
{
    debug_i("WiFi disconnecting...");
    WifiStation.disconnect();
}
/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_onSTAConnect(const String &ssid, MacAddress bssid, uint8_t channel)
{
    debug_i("WiFi Check waiting IP started");
    TimerChkWiFi.initializeMs(lwChkWiFiIpTimeMs, WM_onWiFiDisconnect).startOnce(); // start WiFi connectivity checking when WiFi got IP
}
/*------------------------------------------------------------------------------
DESCRIPTION:
   Will be called when WiFi station becomes fully operational
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_onSTAgotIP(IpAddress ip, IpAddress netmask, IpAddress gateway)
{
    WM_startServersSTA();

    debug_i("WiFi Check started");
    TimerChkWiFi.stop();
    TimerChkWiFi.initializeMs(lwChkWiFiTimeMs, WM_chkWiFi).start(); // start WiFi connectivity checking when WiFi got IP
}

/*------------------------------------------------------------------------------
DESCRIPTION:
 Jus to prevent the browser getting error on requesting this file.
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_onFavicon(HttpRequest &request, HttpResponse &response)
{
    response.code = HTTP_STATUS_OK;
}
/*------------------------------------------------------------------------------
DESCRIPTION:
 
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_startOtherServices()
{
    DT_UpdateSystemTimer();

    ServerComs.addPump(PUMP_ID_1);
    ServerComs.addPump(PUMP_ID_2);
    ServerComs.init(HOST, PATH, AUTHORIZATION, DEVICE_ID);
}

void WM_onSerialRx(Stream &source, char last_c, uint16_t lenght)
{

}
/*------------------------------------------------------------------------------
DESCRIPTION:
 start webpages for station mode and ajax urls.
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_startServersSTA()
{
    // tcpServer.listen(23); //8023
    // start mdns server
    WM_startMDNS();
    // tcpServer.setKeepAlive

    debug_i("=== STA WEB SERVER STARTED ===");
    debug_i("%s", WifiStation.getIP().toString().c_str());
    debug_i("use this url = http://%s/", SysSettings.HostName.c_str());
    debug_i("==========================\r\n");

    // Serial.onDataReceived(WM_onSerialRx);
}
/*------------------------------------------------------------------------------
DESCRIPTION:
 start webpages for access point mode and ajax urls.
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_startServersAP()
{
    // check for file existance
    if (!fileExist("index.html"))
        fileSetContent("index.html", "<h3>Internal Flash storage ERROR! Contact Technician</h3>");

    // Start FTP server
    ftp.listen(21);           // default port
    ftp.addUser("me", "123"); // FTP account

    // start web server
    server.listen(80);                      // default web page port
    server.paths.set("/", WM_onHandleRoot); // for main page
    server.paths.set("/ajax/wifi", WM_onWiFiSettings); // user wifi settings
    server.paths.set("/ajax/config", WM_onServerSettings); // user server settings
    server.paths.set("/favicon.ico", WM_onFavicon); // favicon.ico request
#ifdef CAPTIVE_PORTAL_ENABLED
    server.paths.set("/generate_204", WM_onHandleRoot); // Android captive portal.
    server.paths.set("/fwlink", WM_onHandleRoot);       // Microsoft captive portal.
    server.paths.set("/success.txt", WM_onHandleRoot);  // Firefox captive portal
#endif
    server.paths.setDefault(WM_onFileAP); // default

    // start dns server
    DNS.setTTL(300);
    DNS.setErrorReplyCode(DnsReplyCode::NoError);
    DNS.start(53, "*", WifiAccessPoint.getIP()); // allow for every url

    debug_i("=== AP WEB SERVER STARTED ===");
    debug_i("==========================\r\n");
}
/*------------------------------------------------------------------------------
DESCRIPTION:
 start MDNS service for querying device IP.
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void WM_startMDNS()
{
    responder.begin(SysSettings.HostName.c_str());
    responder.addService(myHttpService);
}