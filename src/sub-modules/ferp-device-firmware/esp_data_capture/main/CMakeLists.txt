idf_component_register(SRCS "hongyang_8_digit.c" "censtarcs_capture.c" "longfeng_capture.c" "main.c"
                    INCLUDE_DIRS "include")

# Set the project name and version as a preprocessor macro
add_definitions(-DPROJECT_NAME="${CMAKE_PROJECT_NAME}" 
                -DPROJECT_VER="${CONFIG_APP_PROJECT_VER}" 
                -DPROJECT_TIME=__TIME__
                -DPROJECT_DATE=__DATE__
               )
