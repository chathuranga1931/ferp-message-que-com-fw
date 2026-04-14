/*******************************************************************************
*                                SPLat Controls                                *
*                          Product Development Group                           *
*                            Melbourne,  AUSTRALIA                             *
*                            http://www.splatco.com                            *
********************************************************************************
   DESCRIPTION:
   This module is the system settings handler.


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

/*==============================================================================

    INCLUDES

==============================================================================*/
#include "server_com.h"
#include <ssl/private_key.h>
#include <ssl/cert.h>
#include <JsonObjectStream.h>

/*==============================================================================

    DEFINES

==============================================================================*/

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
ServerCommunication ServerComs;
NtpClient *ntpClient; // To update system timer

/*==============================================================================

   GLOBAL FUNCTION DEFINITIONS

==============================================================================*/

/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void DT_UpdateSystemTimer(void)
{
    if (ntpClient == nullptr)
    {
        ntpClient = new NtpClient();
        ntpClient->setAutoUpdateSystemClock(true); // auto update system clock
        SystemClock.setTimeZone(10);
    }
}

/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void ServerCommunication::init(String _host, String _path,String _authorization, String _deviceID)
{
    Host = _host;
    Path = _path;
    authorization = _authorization;
    deviceID = _deviceID;
    debug_i("Num of pumps:%d", pumpID.size());
    // postRequest.uri.Scheme = URI_SCHEME_HTTP_SECURE;
    // postRequest.uri.Host = Host;
    // postRequest.uri.Path = Path;
    // postRequest.setMethod(HTTP_POST);

    // postRequest.setHeader(F("Authorization"), authorization);
    // postRequest.setHeader(F("Content-Type"), F("application/json"));
    // // postRequest.setBody(F("{\"points\": [{\"device\": \"ee09d6ec-8f89-47c6-889b-5dd0f22e3432\",\"time\": \"2022-09-15T22:44:55+05:30\",\"measurements\": {\"L\": 2,\"T\": \"1\",\"P\": 900,\"U\": 450},\"events\": {\"PUMPED\": 1}}]}"));

    // auto ssl_init = [](Ssl::Session &session, HttpRequest &request)
    // {
    //     // Go with maximum buffer sizes
    //     // session.maxBufferSize = Ssl::MaxBufferSize::K16;
    //     session.options.verifyLater = true;
    //     // static const Ssl::Fingerprint::Cert::Sha1 sha1Fingerprint PROGMEM{
    //     // 	0x59, 0x92, 0xAD, 0x3F, 0xA5, 0x50, 0x26, 0x53, 0x24, 0xE4,
    //     // 	0x69, 0x50, 0xB2, 0x6C, 0xCE, 0xB4, 0xA6, 0x0C, 0x88, 0x54};
    //     // static const Ssl::Fingerprint::Pki::Sha256 publicKeyFingerprint PROGMEM{
    //     // 	0xCC, 0xDF, 0x30, 0x5E, 0x3C, 0xDA, 0x73, 0xB7, 0x99, 0x0A, 0x16, 0x3C, 0x8D, 0x93, 0x47, 0xA4,
    //     // 	0x1A, 0x17, 0x9D, 0x51, 0x2E, 0x4E, 0x87, 0xFA, 0x87, 0x2F, 0x06, 0xB3, 0xBD, 0x96, 0x1E, 0x40};
    //     // // Trust only a certificate in which the public key matches the SHA256 fingerprint...
    //     // session.validators.pin(publicKeyFingerprint);
    //     // // ... or a certificate that matches the SHA1 fingerprint.
    //     // session.validators.pin(sha1Fingerprint);
    //     session.keyCert.assign(default_private_key, sizeof(default_private_key), default_certificate, sizeof(default_certificate), nullptr);
    // };
    // postRequest.onSslInit(ssl_init);
    // postRequest.onRequestComplete(RequestCompletedDelegate(&ServerCommunication::onRequestCompleted, this));

    auto ctx = this;
    timer.initializeMs(4000, TaskDelegate([ctx]()
                                          {
		ctx->timer.stop();
		if (SystemClock.isSet())
		{
            debug_i("Timer OK");
            ctx->timer.setIntervalMs(30*1000);
            // timer.setCallback(TimerDelegate(&ServerCommunication::sendData, ctx));
            System.queueCallback(TaskDelegate(&ServerCommunication::sendData, ctx));
		}
        // if system time not set, skip connecting to internet.
		ctx->timer.start(); }))
        .startOnce();
};
/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
void ServerCommunication::sendData()
{
    // char body_buffer[4096] = {};
    DynamicJsonDocument jsondata(4096);
    timer.stop();
// curl --location --request POST 'https://http-ingress-alw5epn3aq-el.a.run.app/api/v1/device/data' \
// --header 'Authorization: Basic ZWUwOWQ2ZWMtOGY4OS00N2M2LTg4OWItNWRkMGYyMmUzNDMyOmEwZTFmNjFhLTM4NGEtMTFlZC1hMjYxLTAyNDJhYzEyMDAwMg==' \
// --header 'Content-Type: application/json' \
// --data-raw '{
//     "points": [
//         {
//             "device": "ee09d6ec-8f89-47c6-889b-5dd0f22e3432",
//             "time": "2022-09-15T22:44:55+05:30",
//             "measurements": {
//                 "L": 2,
//                 "T": "1",
//                 "P": 900,
//                 "U": 450
//             },
//             "events": {
//                 "PUMPED": 1
//             }
//         }
//     ]
// }'
    long timestamp = system_ticks();

    jsondata["deviceId"] = deviceID.c_str();
    jsondata["timestamp"] = timestamp;
    JsonArray arrlist = jsondata.createNestedArray("points");

    for (size_t i = 0; i < pumpID.size(); i++)
    {
        auto item = arrlist.createNestedObject();
        item["device"] = pumpID[i].c_str();
        item["timestamp"] = (long)timestamp;
        auto measurement = item.createNestedObject("measurements");
        measurement["L"] = 2;
        measurement["T"] = '1';
        measurement["P"] = 900;
        measurement["U"] = 450;

        auto events = item.createNestedObject("events");
        events["PUMPED"] = (int)1;
    }

    String JsonMsg;
    serializeJson(jsondata, JsonMsg);
    Serial.printf(_F("msg = %s\r\n\r\n"), JsonMsg.c_str());

    HttpRequest *postRequest = new HttpRequest;

    postRequest->uri.Scheme = URI_SCHEME_HTTP_SECURE;
    postRequest->uri.Host = Host;
    postRequest->uri.Path = Path;
    postRequest->setMethod(HTTP_POST);

    postRequest->setHeader(F("Authorization"), authorization);
    postRequest->setHeader(F("Content-Type"), F("application/json"));
    postRequest->setBody(JsonMsg);
    
    auto ssl_init = [](Ssl::Session &session, HttpRequest &request)
    {
        // Go with maximum buffer sizes
        // session.maxBufferSize = Ssl::MaxBufferSize::K16;
        session.options.verifyLater = true;
        // static const Ssl::Fingerprint::Cert::Sha1 sha1Fingerprint PROGMEM{
        // 	0x59, 0x92, 0xAD, 0x3F, 0xA5, 0x50, 0x26, 0x53, 0x24, 0xE4,
        // 	0x69, 0x50, 0xB2, 0x6C, 0xCE, 0xB4, 0xA6, 0x0C, 0x88, 0x54};
        // static const Ssl::Fingerprint::Pki::Sha256 publicKeyFingerprint PROGMEM{
        // 	0xCC, 0xDF, 0x30, 0x5E, 0x3C, 0xDA, 0x73, 0xB7, 0x99, 0x0A, 0x16, 0x3C, 0x8D, 0x93, 0x47, 0xA4,
        // 	0x1A, 0x17, 0x9D, 0x51, 0x2E, 0x4E, 0x87, 0xFA, 0x87, 0x2F, 0x06, 0xB3, 0xBD, 0x96, 0x1E, 0x40};
        // // Trust only a certificate in which the public key matches the SHA256 fingerprint...
        // session.validators.pin(publicKeyFingerprint);
        // // ... or a certificate that matches the SHA1 fingerprint.
        // session.validators.pin(sha1Fingerprint);
        session.keyCert.assign(default_private_key, sizeof(default_private_key), default_certificate, sizeof(default_certificate), nullptr);
    };
    postRequest->onSslInit(ssl_init);
    postRequest->onRequestComplete(RequestCompletedDelegate(&ServerCommunication::onRequestCompleted, this));

    httpclient.send(postRequest);
    debug_i("POST request sent!");
}
/*------------------------------------------------------------------------------
DESCRIPTION:

PARAMETERS:
  -> nil
RETURNS:
 <-  nil
------------------------------------------------------------------------------*/
int ServerCommunication::onRequestCompleted(HttpConnection &connection, bool success)
{
    
    debug_i("=========[ URL: %s ]============", connection.getRequest()->uri.toString().c_str());
    debug_i("Host:%s, Port:%d, Success:%d", connection.getRemoteIp().toString().c_str(), connection.getRemotePort(), success);
    timer.start(); // send data again
    return 0;
}