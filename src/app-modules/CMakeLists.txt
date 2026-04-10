idf_component_register(
    SRCS
        "ticker/ticker.cpp"
        "module_a/module_a.cpp"
        "module_b/module_b.cpp"
        "module_sysmon/module_sysmon.cpp"

    INCLUDE_DIRS
        "."                # app_msg_table.h
        "ticker"           # ticker.h
        "module_a"         # module_a.h
        "module_b"         # module_b.h
        "module_sysmon"    # module_sysmon.h

    PRIV_REQUIRES
        hsys-framework     # hsys_module.h, hsys_pool.h, hsys_types.h, …
        hsys-os            # hsys_soft_timer.h, …
        app                # app_msg_ids.h lives in product/app
)
