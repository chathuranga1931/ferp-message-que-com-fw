idf_component_register(
    SRCS
        "hsys_pool.cpp"
        "hsys_msg.cpp"
        "hsys_module.cpp"
        "hsys_task_mgr.cpp"

    INCLUDE_DIRS
        "."          # exposes hsys_pool.h, hsys_module.h, hsys_msg.h, hsys_task_mgr.h, hsys_log.h etc.

    PRIV_REQUIRES
        hsys-os      # pulls in hsys_task.h, hsys_mutex.h, etc. without ../../
        pal          # pulls in pal_logger.h for FWK_LOG_* macros
)
