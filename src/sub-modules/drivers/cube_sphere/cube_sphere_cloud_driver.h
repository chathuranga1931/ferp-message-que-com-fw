// cube_sphere_cloud_driver.h
//
// Returns the cloud_driver_t instance that wraps the cube_sphere C API.
//
// Usage (app.cpp):
//   #include "cube_sphere_cloud_driver.h"
//   ModuleCloud::instance()->set_driver(cloud_driver_cube_sphere());

#pragma once

#include "cloud_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Returns a pointer to the cube_sphere cloud_driver_t singleton. */
const cloud_driver_t *cloud_driver_cube_sphere(void);

#ifdef __cplusplus
}
#endif
