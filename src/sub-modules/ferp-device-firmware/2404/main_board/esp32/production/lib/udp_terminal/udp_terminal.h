#pragma once

#include "esp_err.h"

#define UDP_PRINT(FMRT, ARGS ...) { char str[100] = {}; const size_t str_size = sprintf(str, FMRT, ##ARGS); udp_server_send((uint8_t*)str, str_size);}

#ifdef __cplusplus
extern "C"
{
#endif


/**
 * Initialise udp terminal
 * Install YAT software and open it using ip address of the board and port 502.
 * Send some text to register port for the software after port opening. 
 * https://sourceforge.net/projects/y-a-terminal/
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t udp_terminal_init();

esp_err_t udp_server_send(uint8_t *src, int len);

#ifdef __cplusplus
}
#endif