#include "app.h"
#include "pal_logger.h"

#include <cstdio>

int main()
{
    pal_logger_init();

    pal_logger_log(true, "[simulator] starting ferp-com-simulator\n");

    app_init();

    pal_logger_log(true, "[simulator] app initialised — entering run loop\n");

    while (true) {
        app_run();
    }

    return 0;
}
