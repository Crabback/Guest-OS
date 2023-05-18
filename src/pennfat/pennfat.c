#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "../pennos/process_control.h"
#include "../pennos/scheduler.h"
#include "../pennos/shell.h"
#include "../pennos/job.h"
#include "../pennos/parser.h"
#include "pennfat_handler.h"
#include "utils.h"

#define PENNFAT_PROMPT "penn-os> "

int fsCommandHandler(char **commands, int commandCount, pennfat **fat) {
    int result = -1;
    char *command = commands[0];

    if (strcmp(command, "mkfs") == 0) { // mkfs
        // Check input format
        #ifdef DEBUGGING
            writeHelper("**** mkfs func ****\n");
        #endif
        if (commands[1] == NULL || commands[2] == NULL || commands[3] == NULL) {
            printf("INPUT FORMAT: [mkfs FS_NAME BLOCKS_IN_FAT BLOCK_SIZE_CONFIG].\n");
            return result;
        }

        result = pennfatMkfs(commands[1], (char) atoi(commands[2]), (char) atoi(commands[3]), fat);
    } else if (strcmp(command, "mount") == 0) { // mount
        #ifdef DEBUGGING
            writeHelper("**** mount func ****\n");
        #endif
        // Check input format
        if (commands[1] == NULL) {
            printf("INPUT FORMAT: [mount FS_NAME].\n");
            return result;
        }

        result = pennfatMount(commands[1], fat);
        #ifdef DEBUGGING
            writeHelper("Mounted fat's name is ");
            writeHelper(commands[1]);
            writeHelper("\n");
        #endif
    }  else if (strcmp(command, "umount") == 0) { // unmount
        result = pennfatUnmount(fat);
    } else if (strcmp(command, "touch") == 0) { // touch
        #ifdef DEBUGGING
            writeHelper("**** touch func ****\n");
        #endif
        // Check input format
        if (commands[1] == NULL) {
            printf("INPUT FORMAT: [touch FILE ...].\n");
            return -1;
        }
        result = pennfatTouch(commands, *fat);
    } else if (strcmp(command, "mv") == 0) { // move
        #ifdef DEBUGGING
            writeHelper("**** mv func ****\n");
        #endif
        // Check input format
        if (commands[1] == NULL || commands[2] == NULL) {
            printf("INPUT FORMAT: [mv SOURCE DEST].\n");
            return -1;
        }

        result = pennfatMove(commands[1], commands[2], *fat);
    } else if (strcmp(command, "rm") == 0) { // rm
        #ifdef DEBUGGING
            writeHelper("**** rm func ****\n");
        #endif
        // Check input format
        if (commands[1] == NULL) {
            printf("INPUT FORMAT: [rm FILE ...].\n");
            return -1;
        }

        result = pennfatRemove(commands, *fat);
    }  else if (strcmp(command, "cat") == 0) { // cat
        #ifdef DEBUGGING
            writeHelper("**** cat func ****\n");
        #endif
        // Check input format
        int cmd_idx = 0;
        while (commands[cmd_idx] != NULL) {
            cmd_idx++;
        }

        if (cmd_idx < 2) {
            printf("INPUT FORMAT: [cat FILE ... [ -w OUTPUT_FILE ]\n cat FILE ... [ -a OUTPUT_FILE ]\n cat -w OUTPUT_FILE\n cat -a OUTPUT_FILE].\n");
            return -1;
        }

        // Check flag
        for (int i = 0; i < cmd_idx; i++) {
            if (strcmp(commands[i], "-w") == 0 && i != cmd_idx - 2) {
                printf("INPUT ERROR: Invalid -w flag location.\n");
                return -1;
            }
            
            if (strcmp(commands[i], "-a") == 0 && i != cmd_idx - 2) {
                printf("INPUT ERROR:Invalid -a flag location.\n");
                return -1;
            }
        }

        result = pennfatCat(commands, cmd_idx, *fat);
    } else if (strcmp(command, "cp") == 0) { // copy
        #ifdef DEBUGGING
            writeHelper("**** cp func ****\n");
        #endif
        // Check input format
        int cmd_idx = 0;
        bool copyingFromHost = false;
        bool copyingToHost = false;

        while (commands[cmd_idx] != NULL) {
            cmd_idx++;
        }

        if (cmd_idx < 3) {
            printf("INPUT FORMAT: [cp [ -h ] SOURCE DEST].\n");
            return -1;
        }

        // Check flag
        for (int i = 0; i < cmd_idx; i++) {
            if (strcmp(commands[i], "-h") == 0) {
                if (i == 1)
                    copyingFromHost = true;
                else if (i == 2) {
                    if (copyingFromHost) {
                        printf("INPUT ERROR: Invalid -h flag location.\n");
                        return -1;
                    }
                    copyingToHost = true;
                } else {
                    printf("INPUT ERROR: Invalid -h flag location.\n");
                    return -1;
                }
            }
        }

        result = pennfatCopy(commands, cmd_idx, copyingFromHost, copyingToHost, *fat);
    } else if (strcmp(command, "ls") == 0) { // ls
        #ifdef DEBUGGING
            writeHelper("**** ls func ****\n");
        #endif
        if(fat == NULL) {
            writeHelper("ERROR: No mounted FAT.\n");
            return -1;
        }
        result = pennfatLs(*fat);
    } else if (strcmp(command, "chmod") == 0) { //chmod
        #ifdef DEBUGGING
            writeHelper("**** chmod func ****\n");
        #endif
        // Check input format
        if (commands[1] == NULL || commands[2] == NULL) {
            printf("INPUT FORMAT: [chmod FILE PERM].\n");
            return -1;
        }

        // Check perm type
        int perm = 0;
        if (strcmp(commands[2], "---") == 0) {
            perm = NONE_PERMS;
        } else if (strcmp(commands[2], "--w") == 0) {
            perm = WRITE_PERMS;
        } else if (strcmp(commands[2], "-r-") == 0) {
            perm = READ_PERMS;
        } else if (strcmp(commands[2], "xr-") == 0) {
            perm = READEXE_PERMS;
        }else if (strcmp(commands[2], "-rw") == 0) {
            perm = READWRITE_PERMS;
        } else if (strcmp(commands[2], "xrw") == 0) {
            perm = READWRITEEXE_PERMS;
        } else {
            printf("PERM ERROR: Permission type must be one of [---, -w-, -r-, xr-, -rw, xrw]\n");
        }

        result = pennfatChmod(commands, perm, *fat);
    } else if (strcmp(command, "show") == 0){
        result = pennfatShow(*fat);
    } else {
        printf("NO SUCH COMMAND: No %s command.\n", commands[0]);
    }

    return result;
}

void exitGracefully(int exitVal, pennfat *fat) {
    if (fat != NULL) {
        freeFat(&fat);
    }
    exit(exitVal);
}

void signalHandler(int sigNum) {
    if (sigNum == SIGINT) {
        // prompt the user and get their inputs
        writeHelper("\n");
        writeHelper("penn-os> ");
    }
}

int main() {
    printf("You're using pennfat.\n");

    // initialize currFat to NULL
    pennfat *currFat = NULL;

    size_t read;
    size_t len = 0;
    char *line = NULL;

    // bind signal handler for sigint
    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("ERROR: Fail to bind signal handler");
        exitGracefully(FAILURE, currFat);
    }

    // interactive mode
    while (1) {
        // prompt the user and get their inputs
        writeHelper("pennfat# ");
        
        if ((read = getline(&line, &len, stdin)) == -1) {
            printf("\n");
            break;
        }

        struct parsed_command *cmd = NULL;
        int i = parse_command(line, &cmd);

        if (i < 0) {
            perror("syntax error: expecting at least one command");
            continue;
        }
        if (i > 0) {
            perror("syntax error: unexpected file input token");
            continue;
        }

        int num_commands = cmd->num_commands; // number of commands
        char **cmd_args = cmd->commands[0];   // array of commands/arguments

        if(num_commands != 0) {
            fsCommandHandler(cmd_args, num_commands, &currFat);
        }
    }
}

/* PROGRESS NOTES:
                        FUNCION_NAME           IMPLEMENTATION       TESTING
commands                fsCommandHandler       Done                 
standalone-fs-shell     main                   *On-going         
*/