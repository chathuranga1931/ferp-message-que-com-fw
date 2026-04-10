#include "app.h"

#include <cstdio>

int main()
{
    printf("[simulator] starting ferp-com-simulator\n");

    app_init();

    printf("[simulator] app initialised — entering run loop\n");

    while (true) {
        app_run();
    }

    return 0;
}
