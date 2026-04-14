idf_component_register(SRCS             "usb_printer.c"
                        INCLUDE_DIRS    "include"
                        REQUIRES        usb
                                        esp_netif
                    )
