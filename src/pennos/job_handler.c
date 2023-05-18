#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job.h"
#include "parser.h"
#include "process_control.h"
#include "process_list.h"
#include "scheduler.h"

int current_id = 0;
extern int fg_pgid;

// Formatter for the "jobs" command
void job_printer(job_node *job, int opt) {
    for (int i = 0; i < job->job_cmds->num_commands; i++) {
        int j = 0;
        while (job->job_cmds->commands[i][j] != NULL) {
            // ignore the "&" symbol in print
            if (strncmp(job->job_cmds->commands[i][j], "&", 1) == 0) {
                continue;
            }
            printf("%s ", job->job_cmds->commands[i][j]);
            j++;
        }
        if (i != job->job_cmds->num_commands - 1)
            printf(" | ");
    }
    if (opt == 1) {
        printf("\n");
    }
}

// Initialize the linkedlist
job_node *initialize_queue() {
    job_node *head = (job_node *)malloc(sizeof(job_node));
    head->prev = NULL;
    head->next = NULL;
    return head;
}

// Add the job to the job queue and return the tail, which is the newly added job.
job_node *add_job(job_node *head, int *pids, int num_commands, struct parsed_command *cmds) {

    // find the last node of the job queue
    job_node *node = head;
    while (node->next != NULL)
        node = node->next;
    job_node *last = node;

    // initialize the added job and add to the end
    job_node *added_job = (job_node *)malloc(sizeof(job_node));
    added_job->prev = last;
    last->next = added_job;
    added_job->next = NULL;

    // record all status
    added_job->job_status = "running";
    added_job->job_status_list = malloc((num_commands + 1) * sizeof(char));
    added_job->job_id = (current_id) + 1;
    current_id++;

    // record all pid
    added_job->job_group_id = pids[0];
    added_job->job_pids = pids;

    // record all commands
    added_job->job_num = num_commands;
    added_job->job_cmds = cmds;

    for (int i = 0; i < added_job->job_num; i++)
        added_job->job_status_list[i] = 'r';

    return added_job;
}

// Remove the given job from the job queue.
void remove_job(job_node *job) {
    job->prev->next = job->next;
    if (job->next != NULL)
        job->next->prev = job->prev;
    free_job(job);
}

// Free the memory of a job node
void free_job(job_node *deleted_job) {
    free(deleted_job->job_pids);
    free(deleted_job->job_cmds);
    // free(deleted_job->job_status);
    free(deleted_job->job_status_list);
    // free(deleted_job->job_cmds);
    free(deleted_job);
}

// Print out the jobs when "jobs" is called
void display_job(job_node *head) {
    if (head->next == NULL)
        return;
    job_node *node = head->next;
    while (node != NULL) {
        printf("[%d] ", node->job_id);
        job_printer(node, 0);
        printf("(%s)\n", node->job_status);
        node = node->next;
    }
}

// find a job node by pid
job_node *find_job_by_pid(job_node *head, int pid) {
    job_node *node = head->next;
    while (node != NULL) {
        for (int i = 0; i < node->job_num; i++) {
            if (node->job_pids[i] == pid)
                return node;
        }
        node = node->next;
    }
    printf("Cannot find the job by pid %d\n", pid);
    return NULL;
}

// Check the finished jobs
void check_if_finished(job_node *head) {
    // find the last node of the job queue
    job_node *node = head->next;
    job_node *last = NULL;
    while (node != NULL) {
        if (node->job_status[0] == 'f') {
            printf("Finished: ");
            job_printer(node, 1);
            if (last == NULL) {
                last = node->prev;
            }
        }
        node = node->next;
    }
    if (last != NULL) {
        job_node *cur = last->next;
        while (cur != NULL) {
            job_node *next = cur->next;
            remove_job(cur);
            cur = next;
        }
        cur_job_node = last;
    }
}

// update the status of a job node for MAC
job_node *change_job_status_mac(job_node *changed_job, int status, int changed_pid) {
    int ind; // find the index of the given pid in the job.
    for (ind = 0; ind < changed_job->job_num; ind++) {
        if (changed_job->job_pids[ind] == changed_pid)
            break;
    }
    // if complete
    if (W_WIFEXITED(status) > 0) {
        changed_job->job_status_list[ind] = 'f';
        bool finished = true;
        // check if all processes in the job node are complete
        for (int i = 0; i < changed_job->job_num; i++) {
            if (changed_job->job_status_list[i] != 'f') {
                finished = false;
                break;
            }
        }
        if (finished) {
            changed_job->job_status = "finished";
        }
    }
    // if signaled for different reasons
    else if (W_WIFSIGNALED(status) != 0) {
        printf("\nWIFSIGNALED\n");
        changed_job->job_status_list[ind] = 'f';
        bool finished = true;
        for (int i = 0; i < changed_job->job_num; i++) {
            if (changed_job->job_status_list[i] != 'f') {
                finished = false;
                break;
            }
        }
        if (finished) {
            changed_job->job_status = "finished";
        }
    }
    // if stopped
    else if (W_WIFSTOPPED(status) > 0) {
        if ((changed_job->job_status_list[ind] == 'r') && (changed_job->job_status[0] == 'r')) {
            changed_job->job_status = "stopped";
            printf("Stopped: ");
            job_printer(changed_job, 1);
        }
    }

    return changed_job;
}

// update the status of a job node
job_node *change_job_status(job_node *changed_job, int status, int changed_pid) {
    int ind; // find the index of the given pid in the job.
    for (ind = 0; ind < changed_job->job_num; ind++) {
        if (changed_job->job_pids[ind] == changed_pid)
            break;
    }

    // if complete
    if (W_WIFEXITED(status) > 0) {
        changed_job->job_status_list[ind] = 'f';
        bool finished = true;

        // check if all processes in the job node are complete
        for (int i = 0; i < changed_job->job_num; i++) {
            if (changed_job->job_status_list[i] != 'f') {
                finished = false;
                break;
            }
        }
        // the job is finished, remove it
        if (finished) {
            changed_job->job_status = "finished";
            printf("Finished: ");
            job_printer(changed_job, 1);
            job_node *last = changed_job->prev;
            remove_job(changed_job);
            return last;
        }
    }
    // if signaled for different reasons, check the completion
    else if (W_WIFSIGNALED(status) != 0) {
        printf("\nWIFSIGNALED\n");
        changed_job->job_status_list[ind] = 'f';
        bool finished = true;
        for (int i = 0; i < changed_job->job_num; i++) {
            if (changed_job->job_status_list[i] != 'f') {
                finished = false;
                break;
            }
        }
        if (finished) {
            changed_job->job_status = "finished";
            job_node *last = changed_job->prev;
            remove_job(changed_job);
            return last;
        }
    }
    // if stopped, update the status of the job node
    else if (W_WIFSTOPPED(status) > 0) {
        if ((changed_job->job_status_list[ind] == 'r') && (changed_job->job_status[0] == 'r')) {
            changed_job->job_status = "stopped";
            printf("Stopped: ");
            job_printer(changed_job, 1);
        }
    }

    return changed_job;
}

// Change a stopped job to running when "fg" is called.
void restart_job(job_node *job) {
    job->job_status = "running";
    printf("Running: ");
    job_printer(job, 1);
    for (int i = 0; i < job->job_num; i++) {
        if (job->job_status_list[i] == 's')
            job->job_status_list[i] = 'r';
    }
    p_kill(job->job_group_id, S_SIGCONT);
}

// Restart and bring a background job to the foreground
job_node *fg(job_node *head, char **cmd_res) {
    job_node *node;
    bool foundStopped = false;

    // look for the job node with the given job id
    if (cmd_res[1] != NULL) {
        int target_id = atoi(cmd_res[1]);
        node = head;
        while (node != NULL) {
            if (node->job_id == target_id) {
                break;
            }
            node = node->next;
        }
    } else // if we dont give the job id
    {
        // fg the most recently stopped node, otherwise the recent one
        node = cur_job_node;
        job_node *cur = head->next;
        while (cur != NULL) {
            if (cur->job_status[0] == 's') {
                foundStopped = true;
                node = cur;
            }
            cur = cur->next;
        }
    }
    // if not found, throw an error
    if (node == NULL | node == head) {
        perror("Error: Invalid job id");
        return node;
    } else {
        // if stopped, restart it and bring it to fg
        if (node->job_status[0] == 's') {
            printf("Restarting: ");
            job_printer(node, 1);
            fg_pgid = node->job_group_id;
            node->job_status = "running";
            printf("Running: ");
            job_printer(node, 1);
            for (int i = 0; i < node->job_num; i++) {
                if (node->job_status_list[i] == 's')
                    node->job_status_list[i] = 'r';
            }
            p_kill(node->job_group_id, S_SIGCONT);
        }
        // if running, bring it to fg
        else if (node->job_status[0] == 'r') {
            fg_pgid = node->job_group_id;
            printf("Running: ");
            job_printer(node, 1);
        }
    }

    // wait for the foreground jobs to complete
    signal(SIGTTOU, SIG_IGN);
    int status;
    job_node *cur_job = node;
    for (int i = 0; i < node->job_num; i++) {
        pid_t w = p_waitpid(node->job_pids[i], &status, 0);
        // Check the status of the process
        if (W_WIFSTOPPED(status)) {
            printf("[%d]+ stopped\n", node->job_id);
            cur_job = change_job_status(node, status, w);
            break;
        }
        if (w <= 0)
            break;
    }

    // give the terminal control back to the shell
    if (W_WIFEXITED(status)) {
        printf("Finished: ");
        job_printer(node, 1);

        // if we fg a stopped one, set the cur_job to the last node
        if (foundStopped) {
            cur_job = head;
            while (cur_job->next != NULL)
                cur_job = cur_job->next;
        } else {
            // otherwise, set the cur_job to the next most recent job
            cur_job = node->prev;
        }
        remove_job(node);
    } else if (W_WIFSIGNALED(status)) {
        if (foundStopped) // set the cur_job to the last node
        {
            cur_job = head;
            while (cur_job->next != NULL)
                cur_job = cur_job->next;
        } else {
            cur_job = node->prev;
        }
        remove_job(node);
    }

    return cur_job;
}

// resume a stopped background job
job_node *bg(job_node *head, char **cmd_res) {
    if (cmd_res[1] != NULL) {
        int target_id = atoi(cmd_res[1]);
        job_node *node = head->next;

        // find the job by job id
        while (node != NULL) {
            if (node->job_id == target_id) {
                break;
            }
            node = node->next;
        }
        if (node == NULL) {
            perror("Restart error: Failed to find the job by given id.");
        } else if (node->job_status[0] == 'r') {
            perror("Restart error: the job is already running");
        } else {
            restart_job(node);
        }
    } else { // if a job id is not given, find the current job or the most recently stopped one.
        job_node *node = cur_job_node;
        if (node == NULL | node == head) {
            perror("Error: no job");
            return cur_job_node;
        }
        while (node->job_status[0] != 's') {
            node = node->prev;
            if (node == head) {
                break;
            }
        }
        if (node == NULL | node == head) {
            perror("Error: no stopped job");
            return cur_job_node;
        }
        restart_job(node);
    }

    return cur_job_node;
}

void free_job_queue(job_node *head) {
    job_node *job = head;

    printf("check 1\n");
    // terminate all running jobs
    while (job != NULL && job->job_status[0] != 's' && job->job_status[0] != 'f') {
        if (p_kill(job->job_pids[0], S_SIGTERM) < 0) {
            perror("free job queue");
            return;
        }
        job = job->next;
    }
    printf("check 2\n");
    // clean all finished and terminated jobs
    job_node *current_job = cur_job_node;
    while (current_job != NULL) {
        job_node *next_job = current_job->next;
        if (current_job->job_status[0] == 's' || current_job->job_status[0] == 'f') {
            free_job(current_job);
        }
        current_job = next_job;
    }
}