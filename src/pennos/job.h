#ifndef HEADER_H_
#define HEADER_H_
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

struct job_node
{
    struct job_node *prev;
    struct job_node *next;
    int job_id;
    int job_group_id;
    int job_num;
    int *job_pids;
    char *job_status;
    char *job_status_list;
    struct parsed_command *job_cmds;
    int fd_in;
    int fd_out;
};

extern int current_id;
extern int fg_pgid;
extern struct job_node *head;
extern struct job_node *cur_job_node;
extern int option;

typedef struct job_node job_node;

void job_printer(job_node *job, int opt);
job_node *add_job(job_node *head, int *pids, int num_commands, struct parsed_command *cmds);
void remove_job(job_node *job);
void free_job(job_node *deleted_job);
job_node *initialize_queue();
job_node *find_job_by_pid(job_node *head, int pid);
job_node *change_job_status(job_node *changed_job, int status, int changed_pid);
job_node *change_job_status_mac(job_node *changed_job, int status, int changed_pid);
void check_if_finished(job_node *head);
void restart_job(job_node *job);
void display_job(job_node *head);
job_node *bg(job_node *head, char **cmd_res);
job_node *fg(job_node *head, char **cmd_res);
void free_job_queue(job_node *head);
#endif
