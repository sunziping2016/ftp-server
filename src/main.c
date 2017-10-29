#include <stdlib.h>
#include "global.h"

int main(int argc, char *const argv[])
{
    global_init();

    // Parse cmdline
    if (global_start(argc, argv) == -1)
        return EXIT_FAILURE;

    global_run();

    global_close();
    return EXIT_SUCCESS;
}
