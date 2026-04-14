
#ifndef __VERSION_H
#define __VERSION_H

/* Version history */
/*
 * 1.0.1 Initial state
 * 1.0.2 Reset to default button added via GPIO 35
 *       Device will reboot after set configurartion API is called.
 * 1.0.3 Fixed the WiFi AP not connected properly
 * 1.0.4 SD Card modifications, optimize SOFT AP speed. 
 *       Changed DNS and SOFT ap WiFi ssid and password
 * 1.0.5 Checking wifi status for every 15 seconds and if not 
 *       connected try reconnecting, if goes into wifi AP mode,
 *       after 3 minutes, restart to connect to configured
 *       wifi settings
 * 2.3.24 Totalizer filtering, time based and value based
 * 2.3.29 Additional log to SD Card for debugging purposes
 * 2.3.43 HongYang 8 Totalizer Changed Checkeing for continous events
 * 2.3.44 Sanki Added
 * 
 * Printer versioning
 * 42.2.14 Uncommented the newlines after the printing, this is done because
 *         of some printers does not have the cutter, so we need additional 
 *         lines on the end of the bill. 
 */

#define FW_VERSION          "3.0.0.4"

#define HW_VERSION          "1.0.0"
#if defined(FERP_COM_2404)
    #define BOARD_VERSION       "2404"
#else
    #define BOARD_VERSION       "2308"
#endif

#define DEVICE_TYPE             "ferp-com"  

#endif //__VERSION_H