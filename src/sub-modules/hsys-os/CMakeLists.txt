# hsys-os: FreeRTOS OS abstraction layer
# Exposes: hsys_task.h, hsys_queue.h, hsys_mutex.h, hsys_semaphore.h,
#          hsys_event.h, hsys_soft_timer.h, hsys_task_append.h
idf_component_register(
    SRCS
        "free_rtos/hsys_task.cpp"
        "free_rtos/hsys_queue.cpp"
        "free_rtos/hsys_mutex.cpp"
        "free_rtos/hsys_semaphore.cpp"
        "free_rtos/hsys_event.cpp"
        "free_rtos/hsys_soft_timer.cpp"
        "free_rtos/hsys_task_append.cpp"

    INCLUDE_DIRS
        "."
)
