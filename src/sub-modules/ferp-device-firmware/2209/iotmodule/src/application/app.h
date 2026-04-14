
#ifndef __APP_H
#define __APP_H

#include <time.h>

#if defined(FERP_COM)
#elif defined(FERP_PRINTER)
#else
#define FERP_COM
#endif

#define FERP_PRINTER_VERSION        ("1.0.0")
#define FERP_COM_VERSION            ("1.0.0")

#if defined(FERP_COM)
#define FW_VERSION      FERP_COM_VERSION
#elif defined (FERP_PRINTER)
#define FW_VERSION      FERP_PRINTER_VERSION
#else
#define FW_VERSION      FERP_COM_VERSION
#endif


void app_init();
void app_run();

#endif //__APP_H