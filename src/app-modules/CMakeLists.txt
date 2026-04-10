idf_component_register(
    SRCS
        "ticker/ticker.cpp"
        "module_a/module_a.cpp"
        "module_b/module_b.cpp"

    INCLUDE_DIRS
        "."              # exposes app_msg_ids.h as <app_msg_ids.h>
        "ticker"         # exposes ticker.h
        "module_a"       # exposes module_a.h
        "module_b"       # exposes module_b.h

    PRIV_REQUIRES
        hsys-framework   # hsys_module.h, hsys_pool.h, hsys_types.h, …
        hsys-os          # hsys_soft_timer.h, …
)
