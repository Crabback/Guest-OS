#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../macros.h"
#include "job.h"
#include "mounted_fat.h"
#include "parser.h"
#include "process_control.h"
#include "process_list.h"
#include "scheduler.h"
#include "shell-behavior.h"

/* ------------------------------------------------------------------------
------------------------- Built-ins List -----------------------
------------------------------------------------------------------------*/
char *builtins[] = {"cat",
                    "sleep n",
                    "busy",
                    "echo",
                    "touch file ...",
                    "rm file ...",
                    "ls",
                    "touch file ...",
                    "mv src dest",
                    "cp src dest",
                    "rm file ...",
                    "chmod",
                    "ps",
                    "kill -[SIGNAL_NAME] pid ...",
                    "zombify",
                    "orphanify",
                    "nice_pid priority pid",
                    "nice priority command [arg]",
                    "man",
                    "bg [job_id]",
                    "fg [job_id]",
                    "jobs",
                    "logout"};

void cmd_cat(char **argv) {
    int cmd_idx = 0;
    while (argv[cmd_idx] != NULL) {
        cmd_idx++;
    }

    if (cmd_idx < 2) {
        printf("INPUT FORMAT: [cat FILE ... [ -w OUTPUT_FILE ]\n cat FILE ... [ -a OUTPUT_FILE ]\n cat -w OUTPUT_FILE\n cat -a OUTPUT_FILE].\n");
        return;
    }

    // Check flag
    for (int i = 0; i < cmd_idx; i++) {
        if (strcmp(argv[i], "-w") == 0 && i != cmd_idx - 2) {
            printf("INPUT ERROR: Invalid -w flag location.\n");
            return;
        }

        if (strcmp(argv[i], "-a") == 0 && i != cmd_idx - 2) {
            printf("INPUT ERROR:Invalid -a flag location.\n");
            return;
        }
    }
    if (pennfatCat(argv, cmd_idx, mounted_fat) == -1) {
        printf("INPUT ERROR: Invalid cat command.\n");
        return;
    }
}

void cmd_sleep(char **argv) {
    int sleep_time = argv[1] == NULL ? 0 : atoi(argv[1]);
    p_sleep(sleep_time * 10);
}

void cmd_busy() {
    while (1) {
    }
}

void cmd_echo(char **argv) {
    int i = 1;
    while (argv[i] != NULL) {
        f_write(fd1_dup, argv[i], strlen(argv[i]));
        f_write(fd1_dup, " ", 1);
        i++;
    }
    f_write(fd1_dup, "\n", 1);
}

int getCommandCount() { return 1; }

void cmd_ls(char **argv) {
    if (pennfatLs(mounted_fat) == -1) {
        printf("Failed to list files.\n");
    }
}

void cmd_touch(char **argv) {
    // Check input format
    if (argv[1] == NULL) {
        printf("INPUT FORMAT: [touch FILE ...].\n");
        return;
    }
    pennfatTouch(argv, mounted_fat);
}

void cmd_mv(char **argv) {
    if (argv[1] == NULL || argv[2] == NULL) {
        printf("Missing src or dest files\n");
        return;
    }
    pennfatMove(argv[1], argv[2], mounted_fat);
}

void cmd_cp(char **argv) {
    // Check input format
    int cmd_idx = 0;
    bool copyingFromHost = false;
    bool copyingToHost = false;

    while (argv[cmd_idx] != NULL) {
        cmd_idx++;
    }

    if (cmd_idx < 3) {
        printf("INPUT FORMAT: [cp [ -h ] SOURCE DEST].\n");
        return;
    }

    // Check flag
    for (int i = 0; i < cmd_idx; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            if (i == 1)
                copyingFromHost = true;
            else if (i == 2) {
                if (copyingFromHost) {
                    printf("INPUT ERROR: Invalid -h flag location.\n");
                    return;
                }
                copyingToHost = true;
            } else {
                printf("INPUT ERROR: Invalid -h flag location.\n");
                return;
            }
        }
    }

    if (pennfatCopy(argv, cmd_idx, copyingFromHost, copyingToHost, mounted_fat) == -1) {
        printf("INPUT ERROR: Invalid cp command.\n");
        return;
    }
}

void cmd_rm(char **argv) {
    if (argv[1] == NULL) {
        printf("INPUT FORMAT: [rm FILE ...].\n");
        return;
    }
    if (pennfatRemove(argv, mounted_fat) == -1) {
        printf("Failed to remove file.\n");
    }
}

void cmd_chmod(char **argv) {
    // Check input format
    if (argv[1] == NULL || argv[2] == NULL) {
        printf("INPUT FORMAT: [chmod FILE PERM].\n");
        return;
    }

    // Check perm type
    int perm = 0;
    if (strcmp(argv[2], "---") == 0) {
        perm = NONE_PERMS;
    } else if (strcmp(argv[2], "--w") == 0) {
        perm = WRITE_PERMS;
    } else if (strcmp(argv[2], "-r-") == 0) {
        perm = READ_PERMS;
    } else if (strcmp(argv[2], "xr-") == 0) {
        perm = READEXE_PERMS;
    } else if (strcmp(argv[2], "-rw") == 0) {
        perm = READWRITE_PERMS;
    } else if (strcmp(argv[2], "xrw") == 0) {
        perm = READWRITEEXE_PERMS;
    } else {
        printf("PERM ERROR: Permission type must be one of [---, -w-, -r-, xr-, -rw, xrw]\n");
    }

    if (pennfatChmod(argv, perm, mounted_fat) == -1) {
        printf("Failed to change file permissions.\n");
    }
}

void cmd_ps() {
    processlists *pl = get_scheduler();
    int max_pid = k_find_max_pid_in_process_pool(pl);

    char *cols = "PID  PPID  PRI  STAT  CMD\n";
    f_write(STDOUT_FILENO, cols, strlen(cols));
    const char *const states[] = {"E", "S", "B", "R", "S", "O", "R", "W", "Z"};

    for (int pid = 0; pid <= max_pid; pid++) {
        pnode *cur_node = k_get_node_by_pid_in_multiple_lists(pl, pid);
        if (cur_node != NULL) {
            char pid[64];
            char ppid[64];
            char priority[64];
            char status[64];
            char cmd[64];
            int padding;
            sprintf(pid, "%d", cur_node->pcb->pid);
            padding = 5 - strlen(pid);
            for (int i = 0; i < padding; i++)
                strcat(pid, " ");
            sprintf(ppid, "%d", cur_node->pcb->ppid);
            padding = 6 - strlen(ppid);
            for (int i = 0; i < padding; i++)
                strcat(ppid, " ");
            sprintf(priority, "%d", cur_node->pcb->process_priority);
            padding = 5 - strlen(priority);
            for (int i = 0; i < padding; i++)
                strcat(priority, " ");
            sprintf(status, "%s", states[cur_node->pcb->status]);
            padding = 6 - strlen(status);
            for (int i = 0; i < padding; i++)
                strcat(status, " ");
            sprintf(cmd, "%s\n", cur_node->pcb->process_name);
            strcat(status, cmd);
            strcat(priority, status);
            strcat(ppid, priority);
            strcat(pid, ppid);
            f_write(STDOUT_FILENO, pid, strlen(pid));
            cur_node = cur_node->next;
        }
    }
}

void cmd_kill(char *argv[]) {
    if (strcmp(argv[1], "-term") == 0) {
        p_kill(atoi(argv[2]), S_SIGTERM);
    } else if (strcmp(argv[1], "-cont") == 0) {
        p_kill(atoi(argv[2]), S_SIGCONT);
    } else {
        p_kill(atoi(argv[2]), S_SIGSTP);
    }
}

void zombie_child() { return; }

void zombify() {
    char *argv[2] = {"zombie_child", NULL};
    p_spawn(zombie_child, argv, STDIN_FILENO, STDOUT_FILENO);
    while (1)
        ;
    return;
}

void orphan_child() {
    while (1)
        ;
}

void orphanify() {
    char *argv[2] = {"orphan_child", NULL};
    p_spawn(orphan_child, argv, STDIN_FILENO, STDOUT_FILENO);
    return;
}

void cmd_man() {
    size_t len = sizeof(builtins) / sizeof(builtins[0]);
    for (size_t i = 0; i < len; i++) {
        printf("%s\n", builtins[i]);
    }
}