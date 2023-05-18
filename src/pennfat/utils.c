#include "utils.h"

void writeHelper(char *message) {
    if (write(STDOUT_FILENO, message, strlen(message)) == -1) {
        perror("ERROR: Fail to write.");
        exit(EXIT_FAILURE);
    }
}