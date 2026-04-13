/**
 * @file pal_mac_logger_uart.cpp
 * @brief macOS / Linux implementation of logger_uart
 *
 * On the simulator there is no real UART.  All output is directed to stdout
 * so that it interleaves correctly with other printf-based output.
 * fflush() is called after every write to prevent buffering artefacts when
 * piping or redirecting the simulator output.
 */

#include "pal_logger_uart.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// logger_uart — constructor / destructor
// ---------------------------------------------------------------------------

logger_uart::logger_uart()  {}
logger_uart::~logger_uart() {}

// ---------------------------------------------------------------------------
// logger_uart — output primitives
// ---------------------------------------------------------------------------

void logger_uart::print_str(const char *str)
{
    if (!str) return;
    fputs(str, stdout);
    fflush(stdout);
}

void logger_uart::print_char_buff(const char *buff)
{
    if (!buff) return;
    fputs(buff, stdout);
    fflush(stdout);
}
