cmake_minimum_required(VERSION 3.16)

# Paths are relative to THIS file (src/product/ferp-com-esp32-idf/).
# EXTRA_COMPONENT_DIRS must point to the PARENT of each component folder.
# IDF scans each listed directory for sub-folders containing a
# CMakeLists.txt with idf_component_register() and names each component
# after its sub-folder:
#
#   hsys-os        <- src/sub-modules/hsys-os/
#   hsys-framework <- src/sub-modules/hsys-framework/
#   app-modules    <- src/app-modules/
#   app            <- src/product/app/
#
set(EXTRA_COMPONENT_DIRS
    "../../sub-modules/hsys-os"
    "../../sub-modules/hsys-framework"
    "../../app-modules"
    "../app"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ferp-com)
