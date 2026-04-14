idf_component_register(SRCS "settings.c" "spiff_mount.c" "production_io.c" "device.c"
                    INCLUDE_DIRS "include")

# set_source_files_properties(
#                         SOURCE "device.c"
#                         PROPERTIES COMPILE_DEFINITIONS
#                         "PROJECT_VER=\"${CONFIG_APP_PROJECT_VER}\"")