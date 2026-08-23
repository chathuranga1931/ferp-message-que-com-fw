idf_component_register(SRCS "raw_capture.c" "raw_capture_spi.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES driver device esp_timer
                    )
