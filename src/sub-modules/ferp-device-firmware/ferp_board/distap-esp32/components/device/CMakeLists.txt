idf_component_register(SRCS "spiff_mount.c" "device.c" "settings.c" "production_io.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES spiffs json driver censtar hongyang longfeng sanki wayn
                    # REQUIRES
                    )

# Set the project name and version as a preprocessor macro
add_definitions(-DPROJECT_NAME="${CMAKE_PROJECT_NAME}" 
                -DPROJECT_VER="${CONFIG_APP_PROJECT_VER}" 
                -DPROJECT_TIME=__TIME__
                -DPROJECT_DATE=__DATE__
               )