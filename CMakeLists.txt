cmake_minimum_required(VERSION 3.16)

# Each directory listed here is scanned for sub-directories that contain
# a CMakeLists.txt with idf_component_register() — those become components.
#
#   hsys-os        ← src/sub-modules/hsys-os/
#   hsys-framework ← src/sub-modules/hsys-framework/
#   app-modules    ← src/app-modules/
#   ferp-com       ← src/product/ferp-com/
#
set(EXTRA_COMPONENT_DIRS
    "src/sub-modules/hsys-os"
    "src/sub-modules/hsys-framework"
    "src/app-modules"
    "src/product/ferp-com"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ferp-message-library)

