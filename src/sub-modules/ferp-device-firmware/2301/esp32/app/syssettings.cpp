/*==============================================================================

   INCLUDES

==============================================================================*/
#include "syssettings.h"
#include <JsonObjectStream.h>

/*==============================================================================

   DEFINES

==============================================================================*/
#define UART_ID_2 2 ///< ID of UART 2

/*==============================================================================

   TYPES

==============================================================================*/

/*==============================================================================

   NATIONAL FUNCTION DECLARATIONS

==============================================================================*/

/*==============================================================================

   GLOBAL VARIABLE DEFINITIONS

==============================================================================*/
HardwareSerial Serial2(UART_ID_2);

/*==============================================================================

   NATIONAL VARIABLE DEFINITIONS

==============================================================================*/

/*==============================================================================

   GLOBAL FUNCTION DEFINITIONS

==============================================================================*/
/*------------------------------------------------------------------------------
DESCRIPTION:
   generate host name
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void SysSettingsStorage::init()
{
    wm_uHash_t uHash;
    uHash.lw = WifiStation.getMacAddress().getHash();
    sHexString = makeHexString(uHash.ab, 4);
    // HostName = "iotmodule" + sHexString; // host name for the local network
    HostName = SystemHostname;
}

/*------------------------------------------------------------------------------
DESCRIPTION:
   loading configuration setting varibales from storage
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void SysSettingsStorage::load()
{
    for (size_t i = 0; i < 3; i++)
    {
        DynamicJsonDocument jsondata(1024);

        if (Json::loadFromFile(jsondata, SYS_SETTINGS_FILE))
        {
            // RTCtrim = jsondata["rtctrim"];
            STAmode = (bool)jsondata["STAmode"];
            STAssid = jsondata["STAssid"].as<const char *>();
            STApass = jsondata["STApass"].as<const char *>();
            Server = jsondata["Server"].as<const char *>();
            Port = jsondata["Port"].as<uint16_t>();
            ID_Module = jsondata["IDModule"].as<const char *>();
            ID_Nozzel_1 = jsondata["IDNozzel1"].as<const char *>();
            FuelType_Nozzel_1 = jsondata["FuelType1"].as<FUELteType>();
            ID_Nozzel_2 = jsondata["IDNozzel2"].as<const char *>();
            FuelType_Nozzel_2 = jsondata["FuelType2"].as<FUELteType>();

            String JsonMsg;
            serializeJson(jsondata, JsonMsg);
            debug_i("Sys Reading = %s", JsonMsg.c_str());
            // debug_i("\r\n");
            return;
        }
        debug_i("Sys reading failed Attemp %d. Trying again!", i);
        delayMilliseconds(50);
    }
    // tried 3 tiems. so loading defaults
    loadDeflt();
}

/*------------------------------------------------------------------------------
DESCRIPTION:
 saving configuration variables as json
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void SysSettingsStorage::save()
{
    String JsonMsg;
    DynamicJsonDocument jsondata(1024);

    // jsondata["rtctrim"] = RTCtrim;
    jsondata["STAmode"] = (bool)STAmode;
    jsondata["STAssid"] = STAssid.c_str();
    jsondata["STApass"] = STApass.c_str();
    jsondata["Server"] = Server.c_str();
    jsondata["Port"] = (uint16_t)Port;
    jsondata["IDModule"] = ID_Module.c_str();
    jsondata["IDNozzel1"] = ID_Nozzel_1.c_str();
    jsondata["FuelType1"] = (uint8_t)FuelType_Nozzel_1;
    jsondata["IDNozzel2"] = ID_Nozzel_2.c_str();
    jsondata["FuelType2"] = (uint8_t)FuelType_Nozzel_2;

    serializeJson(jsondata, JsonMsg);
    debug_i("sys saving = %s", JsonMsg.c_str());
    // Serial.printf(_F("saving = %s\r\n"), JsonMsg.c_str());

    for (size_t i = 0; i < 3; i++)
    {
        DynamicJsonDocument jsondataverify(1024);

        Json::saveToFile(jsondata, SYS_SETTINGS_FILE); // making json text and save it on flash string
        if (fileExist(SYS_SETTINGS_FILE) && Json::loadFromFile(jsondataverify, SYS_SETTINGS_FILE))
        {
            if (
                // jsondataverify["rtctrim"] == RTCtrim &&
                jsondataverify["STAmode"] == (bool)STAmode &&
                jsondataverify["STAssid"] == STAssid.c_str() &&
                jsondataverify["STApass"] == STApass.c_str())
            {
                debug_i("Verified Saved Sys");
                return;
            }
        }
        debug_i("Saved Sys failed Attemp %d. Trying again!", i);
        delayMilliseconds(50);
    }

    debug_i("Saved Sys failed!");
}
/*------------------------------------------------------------------------------
DESCRIPTION:
 set default of all setting and save it.
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void SysSettingsStorage::loadDeflt()
{
    // RTCtrim = 0;      // real time clock trimming value
    STAmode = false;  // stop going into pairing mode by default
    STAssid = "null"; 
    STApass = "null";
    Server = "";
    Port = 1883;
    ID_Module = "";
    ID_Nozzel_1 = "";
    FuelType_Nozzel_1 = FUELke_Petrol_92;
    ID_Nozzel_2 = "";
    FuelType_Nozzel_2 = FUELke_Diesel;

    //debugging
    STAmode = true;
    STAssid = "Optus_1BA38F"; 
    STApass = "thawysandyZRwSV";

    save();
}
/*------------------------------------------------------------------------------
DESCRIPTION:
 return existancy of setting file.
PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
bool SysSettingsStorage::exist()
{
    for (size_t i = 0; i < 3; i++)
    {
        if (fileExist(SYS_SETTINGS_FILE))
            return true;
        delayMilliseconds(50);
    }

    return false;
}

SysSettingsStorage SysSettings;
