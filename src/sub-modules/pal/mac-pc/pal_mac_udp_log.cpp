/**
 * @file pal_mac_udp_log.cpp
 * @brief macOS / simulator stub for the UDP log PAL.
 *
 * The simulator has no WiFi hardware, so all functions are no-ops or return
 * safe defaults.  This lets ModuleUdpLog compile and link on the simulator
 * target without #ifdefs in shared code.
 */

#include "pal_udp_log.h"

void pal_udp_log_init(const char            * /*server_ip*/,
                      uint16_t               /*port*/,
                      const char            * /*mac_no_colon*/,
                      pal_udp_log_wake_fn_t  /*wake_fn*/)
{
    // No-op on the simulator.
}

void pal_udp_log_start(void)  {}
void pal_udp_log_stop(void)   {}

bool pal_udp_log_is_running(void)
{
    return false;
}

void pal_udp_log_sink(const char * header, const char * buf, size_t /*len*/)
{
    // No-op: simulator logs go to stdout only (pal_mac_logger.cpp).
}

void pal_udp_log_drain(void)
{
    // No-op.
}
