idf_component_register(SRCS "settings.c" "spiff_mount.c" "production_io.c" "device.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES json spiffs censtar_pump hongyang_pump longfeng sanki_pump wayn_pump)

# CMake port of the CFLAGS that component.mk added for the legacy Make build
# (see ../../component.mk / ../../main/component.mk): device.c reads these via
# ID_CMD_DEV_VERSION / ID_CMD_DEV_PROJ_NAME / ID_CMD_DEV_TIMEDATE. idf.py's
# CMake build never reads component.mk, so without this device.c fails with
# "PROJECT_VER/PROJECT_NAME/PROJECT_TIME/PROJECT_DATE undeclared".
set_source_files_properties(
                        "device.c"
                        PROPERTIES COMPILE_DEFINITIONS
                        "PROJECT_VER=\"${CONFIG_APP_PROJECT_VER}\";PROJECT_NAME=\"${PROJECT_NAME}\";PROJECT_TIME=__TIME__;PROJECT_DATE=__DATE__")