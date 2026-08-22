idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ""
                    # pthread: not used directly by main.c, but the C++ static-
                    # local-variable init guard in libstdc++ (pulled in by the
                    # wear_levelling component) needs pthread_cond_* at link
                    # time. Declaring it here guarantees libpthread.a is part
                    # of the final executable's link closure.
                    PRIV_REQUIRES device json pthread)

# CMake port of the CFLAGS main/component.mk added for the legacy Make build:
# main.c logs PROJECT_NAME at startup. See components/device/CMakeLists.txt
# for the same fix applied to device.c.
set_source_files_properties(
                        "main.c"
                        PROPERTIES COMPILE_DEFINITIONS
                        "PROJECT_NAME=\"${PROJECT_NAME}\"")