/*******************************************************************************
*                                SPLat Controls                                *
*                          Product Development Group                           *
*                            Melbourne,  AUSTRALIA                             *
*                            http://www.splatco.com                            *
********************************************************************************
   DESCRIPTION:
   API for the system settings.


********************************************************************************
*           Copyright (c) 2021 SPLat Controls. All rights reserved.            *
*  SPLatOS is free for commercial use on SPLat hardware and also free for not  *
* for profit use on non-SPLat hardware.  Commercial use either in whole or in  *
* part on non-SPLat hardware requires permission from SPLat Controls Pty Ltd.  *
*                                                                              *
* SPLatOS and associated documentation and tools are provided "as is", without *
*  warranty of any kind, express or implied, including but not limited to the  *
*     warranties of merchantability, fitness for a particular purpose and      *
*   non-infringement.  In no event shall the authors or copyright holders be   *
*  liable for any claim, damages or other liability, whether in an action of   *
* contract, tort or otherwise, arising from, out of or in connection with the  *
*            software or the use or other dealings in the software.            *
*                                                                              *
*  The above copyright notice and this permission notice shall be included in  *
*              all copies or substantial portions of the Software.             *
*******************************************************************************/

// must be first

/*==============================================================================

    INCLUDES

==============================================================================*/
#pragma once

#include <SmingCore.h>

/*==============================================================================

    DEFINES

==============================================================================*/
#define MAX_NUM_OF_PUMPS 2

/*==============================================================================

    TYPES

==============================================================================*/

class ServerCommunication
{
public:
    ServerCommunication()
    {
        pumpID.clear();
    };

    void init (String _host, String _path,String _authorization, String _deviceID);//(const char *_host, const char *_path, const char *_authorization, const char *_deviceID);
    void start()
    {
        timer.start();
    };
    void addPump(String _pumpID)
    {
        pumpID.add(_pumpID);
        debug_i("add pump:%s", _pumpID.c_str());
    };
    void removePump(String _pumpID)
    {
        pumpID.removeElement(_pumpID);
        debug_i("remove pump:%s", _pumpID.c_str());
    };

private:
    void sendData();
    int onRequestCompleted(HttpConnection &connection, bool success);

    // Url uri;
    String Host;
    String Path;
    String authorization;
    String deviceID;
    Vector<String> pumpID;


    Timer timer;
    HttpClient httpclient;
    // HttpRequest postRequest;
};

/*==============================================================================

    STATIC VARIABLES DECLARATIONS

==============================================================================*/

/*==============================================================================

    GLOBAL VARIABLE DECLARATIONS

==============================================================================*/
extern ServerCommunication ServerComs;
/*==============================================================================

    STATIC FUNCTION DECLARATIONS

==============================================================================*/
inline uint32_t system_ticks(void){ return (RTC.getRtcNanoseconds() / (1000 * 1000)); }; 
inline uint32_t system_timestamp(void) { return RTC.getRtcSeconds(); };
inline bool system_set_timestamp(uint32_t time_s) {  if(time_s<1000){ return false; }  return RTC.setRtcSeconds(time_s); };

/*==============================================================================

    FUNCTION DECLARATIONS

==============================================================================*/
void DT_UpdateSystemTimer();

// curl --location --request POST "https://http-ingress-alw5epn3aq-el.a.run.app/api/v1/device/data" --header "Authorization: BasicZWUwOWQ2ZWMtOGY4OS00N2M2LTg4OWItNWRkMGYyMmUzNDMyOmEwZTFmNjFhLTM4NGEtMTFlZC1hMjYxLTAyNDJhYzEyMDAwMg==" --header "Content-Type: application/json" --data-raw "{\"points\": [{\"device\": \"ee09d6ec-8f89-47c6-889b-5dd0f22e3432\",\"time\": \"2022-09-15T22:44:55+05:30\",\"measurements\": {\"L\": 2,\"T\": \"1\",\"P\": 900,\"U\": 450},\"events\": {\"PUMPED\": 1}}]}"

// curl --location --request POST "https://controller-alw5epn3aq-el.a.run.app/api/v1beta/fs_report" --header "Accept: application/json" --header "Authorization: Basic YTA3ODRhMDItYTgxYy00YjliLTljMmQtMjRmZjI1MDUzZTY2OnRlc3Q=" --header "Content-Type: application/json" --data-raw "{\"filling_station_ids\": [\"999\"],\"fuel_type\": \"1\",\"start_time\": \"2022-09-23T00:59:55Z\"}"
