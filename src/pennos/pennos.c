#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "process_control.h"
#include "scheduler.h"
#include "../macros.h"
#include "shell.h"
#include "logger.h"

int main(int argc, char *argv[]) {

    init_log();

    // start, init, and load shell process
    k_process_control_initiate();

    // Start scheduler after init, swap to the shell process
    k_process_control_start();

    close_log();
    return 0;
}