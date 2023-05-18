#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../macros.h"
#include "job.h"
#include "mounted_fat.h"
#include "parser.h"
#include "process_control.h"
#include "process_list.h"
#include "scheduler.h"
#include "shell.h"
#include "stress.h"

#define DELIM " \t\n"
#define PIPE_DELIM "|"

// const char *PROMPT = "penn-os> ";
job_node *head;
job_node *cur_job_node;
pid_t pid;
int status;
int fg_pgid = 0;
pennfat *mounted_fat;
int fd0_dup;
int fd1_dup;

void shell() {

    fd0_dup = STDIN_FILENO;
    fd1_dup = STDOUT_FILENO;
    // setup a linkedlist to store background jobs
    mounted_fat = NULL;
    head = initialize_queue();
    cur_job_node = head->next;

    pid_t *pids;
    size_t read;
    size_t len = 0;
    char *line = NULL;

    // printf("Shell starts.\n");
    while (1) {

        // printf("Shell: wait for background jobs to complete.\n");
        int changed_pid = p_waitpid(-1, &status, 1);
        if (changed_pid > 0) {
            // if stopped or finished, updated the job node
            cur_job_node = find_job_by_pid(head, changed_pid);
            cur_job_node = change_job_status(cur_job_node, status, changed_pid);
        }

        // prompt the user and get their inputs
        if (f_write(STDOUT_FILENO, PROMPT, sizeof(PROMPT)) == -1) {
            perror("Write error: failed to prompt user to input");
        }
        // read in commands from user
        if ((read = getline(&line, &len, stdin)) == -1) {
            printf("\n");
            p_logout();
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
        // int is_append = cmd->is_file_append;  // if detect ">>"
        char **cmd_args = cmd->commands[0];   // array of commands/arguments

        // printf("Shell: received commands '%s'.\n", line);

        if (num_commands > 0) {
            // printf("Shell: receive '%s'\n", cmd->commands[0][0]);
            if (strncmp(cmd->commands[0][0], "exit", 4) == 0) {
                p_logout();
                continue;

            } else if (strncmp(cmd->commands[0][0], "hang", 4) == 0) {
                hang();
                continue;

            } else if (strncmp(cmd->commands[0][0], "nohang", 4) == 0) {
                nohang();
                continue;

            } else if (strncmp(cmd->commands[0][0], "recur", 5) == 0) {
                recur();
                continue;

            } else if (strncmp(cmd->commands[0][0], "test", 4) == 0) {
                p_ps();
                continue;

            } else if (strncmp(cmd->commands[0][0], "fg", 2) == 0) {
                cur_job_node = fg(head, cmd_args);
                continue;

            } else if (strncmp(cmd->commands[0][0], "bg", 2) == 0) {
                cur_job_node = bg(head, cmd_args);
                continue;

            } else if (strncmp(cmd->commands[0][0], "jobs", 4) == 0) {
                display_job(head);
                continue;

            } else if (strncmp(cmd->commands[0][0], "man", 3) == 0) {
                cmd_man();
                continue;

            } else if (strncmp(cmd->commands[0][0], "logout", 6) == 0) {
                p_logout();
                continue;

            } else if (strncmp(cmd->commands[0][0], "nice_pid", 8) == 0) {
                int priority = atoi(cmd->commands[0][1]);
                pid_t pid = atoi(cmd->commands[0][2]);
                p_nice(pid, priority);
                continue;

            } else if (strncmp(cmd->commands[0][0], "mkfs", 4) == 0) {
                if (cmd->commands[0][1] == NULL || cmd->commands[0][2] == NULL || cmd->commands[0][3] == NULL) {
                    printf("INPUT FORMAT: [mkfs FS_NAME BLOCKS_IN_FAT BLOCK_SIZE_CONFIG].\n");
                    p_logout();
                }
                pennfatMkfs(cmd->commands[0][1], (char)atoi(cmd->commands[0][2]), (char)atoi(cmd->commands[0][3]), &mounted_fat);
                continue;

            } else if (strncmp(cmd->commands[0][0], "mount", 5) == 0) {
                if (cmd->commands[0][1] == NULL) {
                    printf("INPUT FORMAT: [mount FS_NAME].\n");
                    p_logout();
                }
                pennfatMount(cmd->commands[0][1], &mounted_fat);
                continue;

            } else if (strncmp(cmd->commands[0][0], "umount", 6) == 0) {
                pennfatUnmount(&mounted_fat);
                continue;
            }

            // // printf("Shell: redirection.\n");
            // int fd0 = 0;
            // int fd1 = 0;

            // if (cmd->stdin_file != NULL) {
            //     fd0 = f_open(cmd->stdin_file, 0);
            //     fd0_dup = fd0;
            // }
            // if (cmd->stdout_file != NULL) {
            //     if (is_append) {
            //         fd1 = f_open(cmd->stdout_file, 2);
            //     } else {
            //         fd1 = f_open(cmd->stdout_file, 1);
            //     }
            //     fd1_dup = fd1;
            // }

            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);

            pids = malloc((num_commands + 1) * sizeof(pid_t));

            bool is_nice = 0;
            int cmd_start_idx = 0;
            int priority;
            if (strcmp(cmd->commands[i][0], "nice") == 0) {
                is_nice = 1;
                cmd_start_idx = 2;
                priority = atoi(cmd->commands[i][1]);
            }

            char *key = cmd->commands[i][cmd_start_idx];
            // printf("Shell: parsed key '%s'.\n", key);

            if (strcmp(key, "cat") == 0) {
                pids[0] = p_spawn(cmd_cat, &cmd->commands[0][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "sleep") == 0) {
                pids[0] = p_spawn(cmd_sleep, &cmd->commands[0][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "busy") == 0) {
                pids[0] = p_spawn(cmd_busy, &cmd->commands[0][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "echo") == 0) {
                pids[0] = p_spawn(cmd_echo, &cmd->commands[0][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "ls") == 0) {
                pids[0] = p_spawn(cmd_ls, &cmd->commands[0][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "touch") == 0) {
                pids[0] = p_spawn(cmd_touch, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "mv") == 0) {
                pids[0] = p_spawn(cmd_mv, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "cp") == 0) {
                pids[0] = p_spawn(cmd_cp, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "rm") == 0) {
                pids[0] = p_spawn(cmd_rm, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "chmod") == 0) {
                pids[0] = p_spawn(cmd_chmod, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "ps") == 0) {
                pids[0] = p_spawn(cmd_ps, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "kill") == 0) {
                pids[0] = p_spawn(cmd_kill, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "zombify") == 0) {
                pids[0] = p_spawn(zombify, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else if (strcmp(key, "orphanify") == 0) {
                pids[0] = p_spawn(orphanify, &cmd->commands[i][cmd_start_idx], fd0_dup, fd1_dup);
            } else {
                // if (cmd->stdout_file != NULL) {
                //     FILE *infile = fopen(key, "r");
                //     if (infile == NULL) {
                //         perror("fopen (input)");
                //         continue;
                //     }
                //     char buffer[1024];
                //     size_t bytes_read;
                //     while ((bytes_read = fread(buffer, 1, sizeof(buffer), infile)) > 0) {
                //         f_write(fd1_dup, buffer, strlen(buffer));
                //     }
                //     fclose(infile);
                //     continue;
                // } else {
                printf("Shell: command not found.\n");
                continue;
                // }
            }

            if (pids[0] < 0) {
                perror("fork failed");
                p_logout();
            }
            if (is_nice) {
                p_nice(pids[0], priority);
            }

            if (pids[0] == 0) { // Child Process
                // free
            }

            // If background commands: add to the job list
            if (cmd->is_background) {
                // printf("Shell: background job\n");
                cur_job_node = add_job(head, pids, num_commands, cmd);
                printf("Running: ");
                print_parsed_command(cmd);
            }
            // If foreground commands: wait for all child processes to be finished
            else {
                // printf("Shell: foreground job\n");
                for (int i = 0; i < num_commands; i++) {
                    int w = p_waitpid(pids[0], &status, 0);
                    if (W_WIFSTOPPED(status) > 0) {
                        cur_job_node = add_job(head, pids, num_commands, cmd);
                        cur_job_node = change_job_status(cur_job_node, status, w);
                        break;
                    }
                    if (w <= 0) {
                        perror("Waitpid error");
                        break;
                    }
                }
                if (WIFEXITED(status) > 0) {
                    free(cmd);
                    free(pids);
                }
            }
            // if (fd0 != 0) {
            //     close(fd0);
            // }
            // if (fd1 != 1) {
            //     close(fd1);
            // }
            signal(SIGTTOU, SIG_IGN);
        }
    }
    if (line)
        free(line);
    if (head)
        free(head);
}