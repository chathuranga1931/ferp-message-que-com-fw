/*==============================================================================

	INCLUDES

==============================================================================*/
#include <SmingCore.h>

/*==============================================================================

	DEFINES

==============================================================================*/
#define BUF_SIZE (1024)

/*==============================================================================

	TYPES

==============================================================================*/

/*==============================================================================

	STATIC VARIABLES DECLARATIONS

==============================================================================*/
const String WM_capUrlMicrosoft = "msftconnecttest.com";		 // Microsoft;
const String WM_capUrlAndroid = "connectivitycheck.gstatic.com"; // android
const String WM_capUrlAndroidold = "clients3.google.com";		 // Android pre-6.0
const String WM_capUrlFireFox = "detectportal.firefox.com";		 // Firefox
const String WM_capUrlApple1 = "captive.apple.com";				 // Apple
const String WM_capUrlApple2 = "netcts.cdn-apple.com";			 // Apple

const int32_t lwChkWiFiTimeMs = (1 * 60 * 1000);  // 1min. check wifi connectivity
const int32_t lwChkWiFiIpTimeMs = (20 * 1000);  // 20s. check wifi getting ip

/*==============================================================================

	GLOBAL VARIABLE DECLARATIONS

==============================================================================*/

/*==============================================================================

	STATIC FUNCTION DECLARATIONS

==============================================================================*/

/*==============================================================================

	FUNCTION DECLARATIONS

==============================================================================*/
bool WM_captivePortal(HttpRequest &request, HttpResponse &response);
void WM_onHandleRoot(HttpRequest &request, HttpResponse &response);
void WM_onFileAP(HttpRequest &request, HttpResponse &response);
void WM_onWiFiSettings(HttpRequest &request, HttpResponse &response);
void WM_onServerSettings(HttpRequest &request, HttpResponse &response);
void WM_onSTAdisconnect(const String &ssid, MacAddress bssid, WifiDisconnectReason reason);
void WM_chkWiFi();
void WM_onWiFiDisconnect();
void WM_onSTAConnect(const String &ssid, MacAddress bssid, uint8_t channel);
void WM_onSTAgotIP(IpAddress ip, IpAddress netmask, IpAddress gateway);
void WM_onFavicon(HttpRequest &request, HttpResponse &response);
void WM_onTcpClientConnected(TcpClient* client);
bool WM_onTcpClientReceive(TcpClient& client, char* data, int size);
void WM_onTcpClientComplete(TcpClient& client, bool successful);
void WM_onSerialRx(Stream &source, char last_c, uint16_t lenght);
void WM_startOtherServices();
void WM_startServersSTA();
void WM_startServersAP();
void WM_startMDNS();