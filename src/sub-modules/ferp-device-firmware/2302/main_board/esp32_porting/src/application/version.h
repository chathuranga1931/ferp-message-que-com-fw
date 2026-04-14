
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
 */

#define FW_VERSION_COM          "2.1.12"
#define FW_VERSION_PRINTER      "2.0.0"

#ifdef FERP_COM
    #define FW_VERSION  FW_VERSION_COM
#else
    #define FW_VERSION  FW_VERSION_PRINTER
#endif

#endif //__VERSION_H