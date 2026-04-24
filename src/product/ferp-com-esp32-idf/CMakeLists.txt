cmake_minimum_required(VERSION 3.16)

# Library components (each sub-folder has its own idf_component_register()):
#   hsys-os        <- sub-modules/hsys-os/
#   hsys-framework <- sub-modules/hsys-framework/
#   pal            <- sub-modules/pal/   (headers-only PAL interface)
#   bblanchon__arduinojson <- managed_components/bblanchon__arduinojson/
#
# All application sources are compiled as part of the "main" component.
# To add/remove sources, edit main/CMakeLists.txt.
set(EXTRA_COMPONENT_DIRS
    "../../sub-modules/hsys-os"
    "../../sub-modules/hsys-framework"
    "../../sub-modules/pal"
    "../../../src/managed_components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Force-include user_config.h in every translation unit.
add_compile_options(-include "${CMAKE_CURRENT_SOURCE_DIR}/../app/user_config.h")

project(ferp-com)
