#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Simulator-specific initialisation.
 *
 * Equivalent to app_init() but also registers ModuleSimBridge.
 * Call AFTER ModuleSimBridge::instance()->start_server().
 */
void sim_app_init(void);

#ifdef __cplusplus
}
#endif
