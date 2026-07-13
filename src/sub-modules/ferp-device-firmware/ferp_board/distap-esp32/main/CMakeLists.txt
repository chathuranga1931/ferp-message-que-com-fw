idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "")

# spiffs_create_partition_image(storage ../spiffs_image FLASH_IN_PROJECT)

# Set the project name and version as a preprocessor macro
add_definitions(-DPROJECT_NAME="${CMAKE_PROJECT_NAME}" 
                -DPROJECT_VER="${CONFIG_APP_PROJECT_VER}" 
                -DPROJECT_TIME=__TIME__
                -DPROJECT_DATE=__DATE__
               )