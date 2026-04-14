/*==============================================================================

   INCLUDES

==============================================================================*/
#include <SmingCore.h>
#include "syssettings.h"
#include "web_manage.h"

/*==============================================================================

   DEFINES

==============================================================================*/
#define stringify(x) stringify_literal(x)
#define stringify_literal(x) #x

/*==============================================================================

   TYPES

==============================================================================*/

/*==============================================================================

   NATIONAL FUNCTION DECLARATIONS

==============================================================================*/

/*==============================================================================

   GLOBAL VARIABLE DEFINITIONS

==============================================================================*/

/*==============================================================================

   NATIONAL VARIABLE DEFINITIONS

==============================================================================*/

/*==============================================================================

   GLOBAL FUNCTION DEFINITIONS

==============================================================================*/
/*------------------------------------------------------------------------------
DESCRIPTION:
 main function start up
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void init()
{
    // wm_uHash_t uHash;

    Serial.begin(SERIAL_BAUD_RATE);             // communication with Splat Main board
    Serial.systemDebugOutput(true); // Disable debug output to serial
    // Serial.setTimeout(10);
    // Serial.setRxBufferSize(BUF_SIZE);
    // Serial.setTxBufferSize(BUF_SIZE);

    // Serial2.begin(SERIAL_BAUD_RATE); // 115200 by default
    // Serial2.systemDebugOutput(true); // Enable debug output to Serial2

    debug_i("\r\n\r\nFirmware = %s ", stringify(APP_NAME));     // print current firmware versions
    debug_i("System Info = %d", system_get_rst_info()->reason); // print system start reason

    delayMilliseconds(500); // wait until stable

    spiffs_mount(); // Mount file system, in order to work with files

    SysSettings.init(); // initialise predefine variables

    if (SysSettings.exist())
    {
        SysSettings.load(); // load saved parameters
        debug_i("sys param loaded");
    }
    else
    {
        SysSettings.loadDeflt();
        debug_i("sys defaults loaded");
    }

    if (SysSettings.STAmode)
    {
		WifiAccessPoint.enable(false); // disable AP mode
        if (SysSettings.STAssid != "null") // if not default
        {
            // enable wifi station mode. do web server on local network
            WifiStation.enable(true); // enable WiFi sation
            WifiStation.config(SysSettings.STAssid, SysSettings.STApass, true, true); // auto connect on start and saving details
            WifiStation.setHostname(SystemHostname);						  // type this name on web browser to find the device
            // Run WEB server on wifi connection ready
            WifiEvents.onStationConnect(WM_onSTAConnect);
            WifiEvents.onStationGotIP(WM_onSTAgotIP);
            WifiEvents.onStationDisconnect(WM_onSTAdisconnect); // wifi disconnect event to manage wifi connection
        }
        else
        {
            WifiStation.enable(false); // disable WiFi station
        }
        System.onReady(WM_startOtherServices);
    }
    else
    {
        WifiAccessPoint.setIP(IpAddress(8, 8, 8, 8));
        WifiAccessPoint.enable(true);
		WifiAccessPoint.config("PumpConfig_" + SysSettings.sHexString, "", AUTH_OPEN);
        System.onReady(WM_startServersAP);
    }


    // debug_i("MAC STA: %s", WifiStation.getMacAddress().toString().c_str()); // print current mac address
    debug_i("Hash: %s, hostname: %s", SysSettings.sHexString.c_str(), SysSettings.HostName.c_str());
}
