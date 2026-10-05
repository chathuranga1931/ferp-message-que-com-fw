cmake_minimum_required(VERSION 3.16)

# FERP receipt printer — USB model (ESP32-S3 USB OTG host).
# Same component layout as the ferp-com products; all application sources are
# compiled into the "main" component (see main/CMakeLists.txt).
set(EXTRA_COMPONENT_DIRS
    "../../../sub-modules/hsys-os"
    "../../../sub-modules/hsys-framework"
    "../../../sub-modules/pal"
    "../../../managed_components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Force-include user_config.h in every translation unit.
add_compile_options(-include "${CMAKE_CURRENT_SOURCE_DIR}/../app/user_config.h")

project(ferp-printer-usb)

# SPIFFS image from data/ (only written by a serial flash — OTA never touches SPIFFS).
spiffs_create_partition_image(spiffs data FLASH_IN_PROJECT)
