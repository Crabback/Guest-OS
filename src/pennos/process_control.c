#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#include "../macros.h"
#include "process_control.h"
#include "process_list.h"
#include "scheduler.h"
#include "shell.h"
#include "logger.h"
#include "ucontext_func.h"

#define STACK_SIZE 16384

// CPU Supervision bit kernel mode value
int cpu_mode = KERNEL_MODE;

// time quantum in ms
static const int centisecond = 10000; // 10 milliseconds

static ucontext_t mainContext;
static ucontext_t schedulerContext;
static ucontext_t returnerContext;
static ucontext_t idleContext;

bool idle_flag = false;

// process control queues/lists
PCB *pcb_list[MAX_PROCESSES];
processlists *PL = NULL;
PCB *current_pcb = NULL;
PCB *fg_pcb = NULL;
PCB *shell_pcb = NULL;
int current_pid = 0;

// kernel thread
void kernel_thread_scheduler();
void kernel_thread_returner();
void kernel_thread_idle();

// todo
//  helper functions for running
int cpu_time = 0;

// protection signal blocking masks
static sigset_t protect_sigmask;
// static sigset_t old_sigmask;
static sigset_t sigmask_stack[MAX_PROTECTION_LAYERS];
static int cpu_mode_stack[MAX_PROTECTION_LAYERS];
static unsigned int protection_level = 0; // use to count
void k_enter_protected_mode();
void k_leave_last_protected_mode();
pid_t last_scheduled_pid = 0;

// signals
void sig_handler(int signo) {
    if (signo == SIGTSTP) {
        // ctrl + Z
        // printf("\nsig_handler: sigtstp recieved\n");
        // printf("sig_handler: current_pcb->process_name = %s\n", current_pcb->process_name);
        if (current_pcb->pid == shell_pcb->pid) {
            // prompt the user and get their inputs
            printf("sig_hanlder ignore ctrl+Z\n");
            if (write(STDOUT_FILENO, PROMPT, sizeof(PROMPT)) == -1) {
                perror("Write error: failed to prompt user to input");
            }
        } else {
            printf("\n");
            p_kill(current_pcb->pid, S_SIGSTP);
        }

    } else if (signo == SIGINT) {
        // ctrl + C
        // printf("\nsig_handler: sigint recieved\n");
        // printf("sig_handler: current_pcb->process_name = %s\n", current_pcb->process_name);
        if (current_pcb->pid == shell_pcb->pid) {
            printf("sig_hanlder ignore ctrl+C\n");
            // prompt the user and get their inputs
            if (write(STDOUT_FILENO, PROMPT, sizeof(PROMPT)) == -1) {
                perror("Write error: failed to prompt user to input");
            }
        } else {
            p_kill(current_pcb->pid, S_SIGTERM);
            printf("\n");
        }
    }
}

void k_sig_reg() {
    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        printf("Unable to catch SIGINT\n");
    }
    if (signal(SIGTSTP, sig_handler) == SIG_ERR) {
        printf("Unable to catch SIGTSTP\n");
    }
}

// Kernel
// Running handler
static void alarm_handler(int signum) // SIGALRM
{
    if (current_pcb != NULL)
        // printf("Handle SIGALARM in %s [pid=%d] context\n", current_pcb->process_name, current_pcb->pid);
        cpu_time++;
    // printf("TIME+1=%d\n", cpu_time);

    if (cpu_mode == KERNEL_MODE) {
        // printf("SIGALARM in kernel mode.\n");
        return;
    }
    // update sleep list
    pnode *sleep_node = PL->sleep_list->next;
    while (sleep_node != PL->sleep_list) {
        // decrease the sleeping time
        // printf("alarm_handler: pid=%d remaining=%d time--\n", sleep_node->pcb->pid, sleep_node->pcb->sleep_remain);
        sleep_node->pcb->sleep_remain--;
        if (sleep_node->pcb->sleep_remain < 0 && sleep_node->pcb->status == BLOCKED) {
            // move if the sleeped process isn't stopped
            pnode *next_node = sleep_node->next;
            PCB *awake_pcb = sleep_node->pcb;
            k_delete(PL->sleep_list, awake_pcb);
            k_delete(PL->blocked_queue, awake_pcb);
            k_add_tail(PL->list_pointer[awake_pcb->process_priority + 1], awake_pcb);
            sleep_node = next_node;
            // update status
            // printf("alarm_handler: pid=%d awake\n", sleep_node->pcb->pid);
            if (sleep_node->pcb) {
                sleep_node->pcb->sleep_remain--;
            }
            awake_pcb->status = READY;
            log_process_change("UNBLOCKED", awake_pcb->pid, awake_pcb->process_priority,
                               awake_pcb->process_name);
        } else {
            sleep_node = sleep_node->next;
        }
    }
    logger_flush();
    // When time quantum finished, swap to scheduler context!
    if (current_pcb == NULL || idle_flag) {
        // printf("alarm_handler swap to scheduler without saving\n");
        setcontext(&schedulerContext);
    } else {
        // printf("alarm_handler swap to scheduler and save to %s [pid=%d]\n", current_pcb->process_name, current_pcb->pid);
        swapcontext(current_pcb->ucp, &schedulerContext);
    }
}

static void k_set_alarm_handler(void (*handler)()) {
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = SA_RESTART;
    sigfillset(&act.sa_mask);
    sigaction(SIGALRM, &act, NULL);
}

static void k_set_timer(void) {
    struct itimerval it;
    it.it_interval = (struct timeval){.tv_usec = centisecond * 10};
    it.it_value = it.it_interval;
    setitimer(ITIMER_REAL, &it, NULL);
}

// static void k_free_stacks(void) {
//     free(schedulerContext.uc_stack.ss_sp);

//     for (int i = 0; i < THREAD_COUNT; i++)
//         free(threadContexts[i].uc_stack.ss_sp);
// }
// Process control functions

void k_unblock_wait(processlists *pl, PCB *pcb) {
    pnode *waiting_node = pl->waiting_list->next;
    while (waiting_node != pl->waiting_list) {
        if (waiting_node->pcb->pid_waitfor == pcb->pid || (waiting_node->pcb->pid_waitfor == -1 && waiting_node->pcb->pid == pcb->ppid)) {
            // find one guy in waiting list waiting for current exit process
            // printf("UNBLOCKING\n");
            PCB *unblocking_parent = waiting_node->pcb;
            k_delete(pl->waiting_list, unblocking_parent);
            k_delete(pl->blocked_queue, unblocking_parent);
            k_add_tail(pl->list_pointer[unblocking_parent->process_priority + 1], unblocking_parent);
            // printf("UNBLOCK FINISH\n");
            log_process_change("UNBLOCKED", unblocking_parent->pid,
                               unblocking_parent->process_priority,
                               unblocking_parent->process_name);
            unblocking_parent->status = READY;
            break;
        }
        waiting_node = waiting_node->next;
    }
}
int k_terminate(PCB *pcb, bool signaled) {
    // ! real exit implementation inside kernel
    // do nothing if we are terminating an already terminated process
    if (k_get_node_by_pid_in_multiple_lists(PL, pcb->pid) == NULL) {
        printf("invalid pid in system call\n");
        return FAILURE;
    }
    if (pcb->status == EXITED || pcb->status == SIGNALED) {
        return SUCCESS;
    }

    if (signaled) {
        log_process_change("SIGNALED", pcb->pid, pcb->process_priority,
                           pcb->process_name);
    } else {
        log_process_change("EXITED  ", pcb->pid, pcb->process_priority,
                           pcb->process_name);
    }

    // check whether marked as Orphan, manipulate input pcb and waitpid
    pid_t pid = pcb->pid;
    // clean
    k_delete_in_processlists(PL, pcb->pid);
    k_delete(PL->waiting_list, pcb);
    k_delete(PL->sleep_list, pcb);
    if (pcb->status == ORPHAN) {
        //- destroy PCB and clean up p_nodes; (no need to check waitpid stuffs,
        // becasue parents die)
        printf("k_terminate: clean an orphan [%s] [pid=%d] [ppid=%d]\n", pcb->process_name, pcb->pid, pcb->ppid);
        k_process_cleanup(pcb);
    } else {
        // add pcb to zombie list
        k_add_tail(PL->zombie_queue, pcb);
        // change the pcb status to EXITED
        log_process_change("ZOMBIE  ", pcb->pid, pcb->process_priority,
                           pcb->process_name);
        pcb->status = signaled ? SIGNALED : EXITED;
        // check waiting list, unblock
        k_unblock_wait(PL, pcb);
    }

    // handling all children
    // find all children
    for (int i = 0; i < TOTAL_NUM_LIST; i++) {
        pnode *head = PL->list_pointer[i];
        pnode *node = head->next;
        while (node != head) {
            if (node->pcb->ppid == pid) {
                // find one child
                PCB *child_pcb = node->pcb;
                child_pcb->status = ORPHAN;
                log_process_change("ORPHAN  ", child_pcb->pid, child_pcb->process_priority, child_pcb->process_name);
                // reset the node to head and later rescan
                node = head; // ! this step is critical
                // kill this child by SIGTERM
                if (k_process_kill(child_pcb, S_SIGTERM)) {
                    return FAILURE;
                }
            }
            node = node->next;
        }
    }
    return SUCCESS;
}

// kernel threads
// Scheduler
void kernel_thread_scheduler() {
    cpu_mode = KERNEL_MODE;
    // printf("In scheduler:\n");
    // check empty
    int current_queue_num = 0;
    for (int i = 0; i < NUM_PRIORITY_LEVELS; i++) {
        pnode *head = PL->list_pointer[i];
        if (head->next != head) {
            current_queue_num++;
        }
    }
    // printf("in scheduler count=%d\n", current_queue_num);
    if (current_queue_num > 0) {
        // if has at least one ready process
        idle_flag = false;
        k_enter_protected_mode();
        // select next thread, give next step a head
        int ready_queue_id = k_schedule(PL);
        k_leave_last_protected_mode();

        k_enter_protected_mode();
        pnode *head = PL->list_pointer[ready_queue_id];
        // move the selection to the tail
        current_pcb = head->next->pcb;
        k_delete(head, current_pcb);
        k_add_tail(head, current_pcb);
        k_leave_last_protected_mode();
        // swap to the thread
        // printf("scheduler: set context to %s's ucontext\n", current_pcb->process_name);
        // if (current_pcb->pid != 1 && last_scheduled_pid != 1) {
        //     log_schedule_event(current_pcb->pid, current_pcb->process_priority,
        //                        current_pcb->process_name);
        // }
        if (current_pcb != NULL)
            // printf("The ucontext being set to: ucp=%p uc_link=%p\n", current_pcb->ucp, current_pcb->ucp->uc_link);
            cpu_mode = USER_MODE;
        setcontext(current_pcb->ucp);
    } else {
        // run idle
        // printf("scheduler: select idle\n");
        idle_flag = true;
        pnode *temp = k_get_node_by_pid_in_multiple_lists(PL, shell_pcb->pid_waitfor);
        if (temp)
            current_pcb = temp->pcb;
        cpu_mode = USER_MODE;
        setcontext(&idleContext);
    }
    exit(EXIT_FAILURE);
}
// returner
void kernel_thread_returner() {
    cpu_mode = KERNEL_MODE;
    // OS helps to manually call k_terminate() on just returned process
    k_enter_protected_mode();
    k_terminate(current_pcb, false);
    k_leave_last_protected_mode();
    usleep(10);
    // printf("Returner -- handle an automatically returned process\n");
    setcontext(&schedulerContext);
}

// idle
void kernel_thread_idle() {
    // printf("IDLE\n");
    while (true) {
        usleep(1000000);
    }
    // sig suspend
    setcontext(&schedulerContext);
    exit(EXIT_FAILURE);
}

// Kernel Interface
int k_process_control_initiate() {
    // create process lists
    PL = k_create_process_lists();
    // prepare kernel threads
    k_make_context(&schedulerContext, NULL, kernel_thread_scheduler, NULL, KERNEL_THREAD);
    k_make_context(&returnerContext, NULL, kernel_thread_returner, NULL, KERNEL_THREAD);
    k_make_context(&idleContext, NULL, kernel_thread_idle, NULL, USER_THREAD); // idle need to receive SIGALARM
    k_sig_reg();
    // prepare clock
    k_set_alarm_handler(alarm_handler);
    // spawn the shell
    char *shell_argv[2] = {"shell", NULL};
    pid_t shell_pid = p_spawn(shell, shell_argv, STDIN_FILENO, STDOUT_FILENO);
    if (k_get_node_by_pid_in_multiple_lists(PL, shell_pid) == NULL) {
        printf("invalid pid in system call\n");
        return FAILURE;
    }
    current_pid = shell_pid;
    shell_pcb = k_get_node_by_pid_in_multiple_lists(PL, shell_pid)->pcb;
    return SUCCESS;
}

int k_process_control_start() {
    // setup clock
    k_set_timer();
    // the last step is to jump to scheduler
    swapcontext(&mainContext, &schedulerContext);
    return SUCCESS;
}

PCB *k_process_create(PCB *parent) {
    PCB *pcb = malloc(sizeof(PCB));
    if (pcb == NULL) {
        perror("malloc");
        return NULL;
    }

    pcb->pid = k_find_available_pid(PL);
    pcb->num_children = 0;
    pcb->process_priority = MED;
    pcb->status = READY;
    if (parent == NULL) {
        pcb->ppid = 0;
    } else {
        pcb->ppid = parent->pid;
    }

    pcb->process_name[0] = '\0';
    pcb->ucp = &(pcb->context);
    getcontext(&pcb->context);
    pcb->context.uc_link = 0;
    pcb->context.uc_stack.ss_sp = malloc(STACK_SIZE);
    pcb->context.uc_stack.ss_size = STACK_SIZE;
    pcb->context.uc_stack.ss_flags = 0;
    sigemptyset(&(pcb->context.uc_sigmask));
    pcb->context.uc_link = NULL;

    // inherit parent fds
    if (parent == NULL) {
        pcb->stdin = STDIN_FILENO;
        pcb->stdout = STDOUT_FILENO;
    } else {
        pcb->stdin = parent->stdin;
        pcb->stdout = parent->stdout;
    }

    pcb_list[pcb->pid] = pcb;
    if (parent != NULL) {
        parent->children[parent->num_children++] = pcb->pid;
    }
    return pcb;
}

int k_process_cleanup(PCB *process) {
    if (process) {
        pcb_list[process->pid] = NULL;
        free(process->context.uc_stack.ss_sp);
        free(process);
    }
    return SUCCESS;
}

int k_process_kill(PCB *process, int signal) {
    pnode *node = k_get_node_by_pid_in_multiple_lists(PL, process->pid);
    if (node == NULL) {
        printf("invalid pid in system call\n");
        return FAILURE;
    }
    // p_kill(process->pid, signal);
    if (signal == S_SIGCONT) {
        printf("k_process_kill: SIGCONT\n");
        if (process->status == STOPPED) {
            k_delete(PL->stopped_queue, process);
            if (k_get_node_by_pid_in_single_list(PL->sleep_list, process->pid) != NULL) {
                // if before stop, is blocked by sleep, move back to blocked
                k_add_tail(PL->blocked_queue, process);
                process->status = BLOCKED;
            } else {
                printf("SIGCONT: move pid=%d to ready [%d]\n", process->pid, process->process_priority + 1);
                k_add_tail(PL->list_pointer[process->process_priority + 1], process);
                process->status = READY;
            }
            // log
            log_process_change("CONTINUED", process->pid, process->process_priority,
                                   process->process_name);
        }
    }
    if (signal == S_SIGSTP) {
        printf("k_process_kill: SIGSTP\n");
        if (process->status == READY || k_get_node_by_pid_in_single_list(PL->sleep_list, process->pid) != NULL) {
            // if the process is in a ready liss or a blocked sleep lists, move the process to stopped list
            k_delete_in_processlists(PL, process->pid);
            k_add_tail(PL->stopped_queue, process);
            // update the status
            process->status = STOPPED;
            // log
            log_process_change("STOPPED  ", process->pid, process->process_priority,
                                   process->process_name);
            // unblock waited pid
            k_unblock_wait(PL, process);
            if (process == current_pcb) {
                // if send SIGSTOP to myself; this can happen when user app
                // calls p_kill in its code itself.
                k_leave_last_protected_mode();
                // printf("STOP current_active_process: %s\n", current_pcb->process_name);
                usleep(10);
                if (idle_flag) {
                    setcontext(&schedulerContext);
                } else {
                    swapcontext(current_pcb->ucp, &schedulerContext);
                }
                usleep(10);
                k_enter_protected_mode();
            }
        }
    }

    if (signal == S_SIGTERM) {
        printf("k_process_kill: SIGTERM\n");
        if (k_terminate(process, true)) {
            return FAILURE;
        }
    }
    return SUCCESS;
}

// User Interface
pid_t p_spawn(void (*func)(), char *argv[], int fd0, int fd1) {
    k_enter_protected_mode();
    PCB *pcb = k_process_create(current_pcb);
    if (pcb == NULL) {
        return FAILURE; // -1
    }
    pcb->stdin = fd0;
    pcb->stdout = fd1;
    strcpy(pcb->process_name, argv[0]);
    // make ucontext, all spawned processes should go to returner if returned
    if (k_make_context(pcb->ucp, &returnerContext, func, argv, USER_THREAD)) {
        printf("ucontext create failed in spawn\n");
        return FAILURE; // -1
    }
    pcb->process_priority = func == shell ? HIGH : MED; // default is med

    // put into ready queue, initialy all put to med queue and shell to high
    pnode *insert_queue_head = func == shell ? PL->ready_queue_high : PL->ready_queue_med;
    k_add_head(insert_queue_head, pcb);
    // printf("p_spawn: pid=%d, ppid=%d, name=%s, priority=%d\n", pcb->pid, pcb->ppid, pcb->process_name, pcb->process_priority);
    // scheduler_add_process(new_process);
    log_process_change("CREATE  ", pcb->pid, pcb->process_priority, pcb->process_name);
    k_leave_last_protected_mode();
    return pcb->pid;
}

void p_sleep(unsigned int ticks) {
    if (ticks == 0) {
        return;
    }
    k_enter_protected_mode();
    // printf("Enter sleep\n");
    // current process must be in ready status
    k_delete_in_processlists(PL, current_pcb->pid);
    k_add_tail(PL->blocked_queue, current_pcb);
    current_pcb->sleep_remain = ticks;
    current_pcb->status = BLOCKED;
    log_process_change("BLOCKED  ", current_pcb->pid, current_pcb->process_priority,
                       current_pcb->process_name);
    k_add_tail(PL->sleep_list, current_pcb);
    // printf("Sleep begins for pid=%d and %d ticks\n", current_pcb->pid, ticks);
    k_leave_last_protected_mode();
    swapcontext(current_pcb->ucp, &schedulerContext);
    // printf("p_sleep: return to sleep context\n");
}

pid_t p_waitpid(pid_t pid, int *wstatus, bool nohang) {
    k_enter_protected_mode();
    // printf("p_waitpid: enter waitpid args: pid=%d nohang=%d \n", pid, nohang);
    pid_t waited_pid;

    // check error
    pnode *child_node = NULL;
    if (pid >= 0) {
        child_node = k_get_node_by_pid_in_multiple_lists(PL, pid);
        if (child_node == NULL) {
            printf("invalid pid in system call\n");
            k_leave_last_protected_mode();
            return FAILURE;
        }
    }
    if (k_get_node_by_pid_in_multiple_lists(PL, current_pcb->pid) == NULL) {
        // if current process doesn't have child, directly return -1
        printf("waitpid parent has no child\n");
        k_leave_last_protected_mode();
        return FAILURE;
    }

    // try to find waited pid
    pnode *node;
    if (pid == -1) { // find any zombie child or stopped child of current process
        // node is parent node
        node = k_get_node_by_ppid_in_single_list(PL->zombie_queue, current_pcb->pid);
        if (node == NULL)
            node = k_get_node_by_ppid_in_single_list(PL->stopped_queue, current_pcb->pid);
    } else if (pid >= 0) { // find specific pid
        node = k_get_node_by_pid_in_single_list(PL->zombie_queue, pid);
        if (node == NULL)
            node = k_get_node_by_pid_in_single_list(PL->stopped_queue, pid);
    } else {
        // not support pid < -1 (group id)
        printf("invalid pid in system call\n");
        k_leave_last_protected_mode();
        return FAILURE;
    }

    // handle the not found case
    if (node == NULL) {
        if (nohang) {
            // if non-blocking mode, directly return 0
            if (pid >= 0) { // if nohang and specify the pid, update the status
                if (wstatus != NULL)
                    *wstatus = child_node->pcb->status;
            }
            k_leave_last_protected_mode();
            return 0;
        } else {
            // if not found, and is in blocking mode, block until it can be found
            // 1 add a new node to waiting list
            k_add_tail(PL->waiting_list, current_pcb);
            // 2 update the pid_waiting_for flag in PCB
            current_pcb->pid_waitfor = pid;
            // 3 move current pcb node to blocking list
            k_delete_in_processlists(PL, current_pcb->pid);
            k_add_tail(PL->blocked_queue, current_pcb);
            current_pcb->status = BLOCKED;
            log_process_change("BLOCKED  ", current_pcb->pid,
                               current_pcb->process_priority, current_pcb->process_name);
            // 4 swap to scheduler
            // printf("p_waitpid: to block, before swap to sched, save [pid=%d]%s\n", current_pcb->pid, current_pcb->process_name);
            k_leave_last_protected_mode();
            usleep(10);
            swapcontext(current_pcb->ucp, &schedulerContext);
            usleep(10);
            k_enter_protected_mode();
            // printf("re-enter p_wait_pid: %d\n", pid);
            // 5 (after un-blocking) find pid in zombie list and stopped list
            if (pid == -1) { // find any zombie child or stopped child of
                             // current process
                node = k_get_node_by_ppid_in_single_list(PL->zombie_queue, current_pcb->pid);
                if (node == NULL) {
                    node = k_get_node_by_ppid_in_single_list(PL->stopped_queue, current_pcb->pid);
                }
            } else { // find specified pid
                node = k_get_node_by_pid_in_single_list(PL->zombie_queue, pid);
                if (node == NULL) {
                    node = k_get_node_by_pid_in_single_list(PL->stopped_queue, pid);
                }
            }
            if (node == NULL) {
                printf("invalid pid in system call\n");
                k_leave_last_protected_mode();
                return FAILURE;
            }
        }
    }
    // Handle the found case
    // update wstatus
    if (wstatus != NULL)
        *wstatus = node->pcb->status;
    waited_pid = -1;
    if (node->pcb->status == STOPPED) {
        // stopped child
        PCB *stopped_pcb = node->pcb;
        waited_pid = stopped_pcb->pid;
        log_process_change("WAITEDSTP", waited_pid, stopped_pcb->process_priority,
                           stopped_pcb->process_name);
    } else {
        // zombie child
        PCB *zombie_pcb = node->pcb;
        waited_pid = zombie_pcb->pid;
        log_process_change("WAITED  ", waited_pid, zombie_pcb->process_priority,
                           zombie_pcb->process_name);
        // clean the found zombie node
        k_delete(PL->zombie_queue, zombie_pcb);
        k_process_cleanup(zombie_pcb);
    }
    k_leave_last_protected_mode();
    return waited_pid;
}

int p_kill(pid_t pid, int sig) {
    k_enter_protected_mode();
    pnode *node = k_get_node_by_pid_in_multiple_lists(PL, pid);
    if (node == NULL) {
        printf("invalid pid in system call\n");
        return FAILURE;
    }
    int ret = k_process_kill(node->pcb, sig);
    k_leave_last_protected_mode();
    return ret;
}

int p_nice(pid_t pid, int priority) {
    if (priority > 1 || priority < -1) {
        printf("error in changing priority\n");
        return FAILURE;
    }
    k_enter_protected_mode();
    // check whether pid can be found
    pnode *node = k_get_node_by_pid_in_multiple_lists(PL, pid);
    if (node == NULL) {
        printf("invalid pid in system call\n");
        k_leave_last_protected_mode();
        return FAILURE;
    }
    PCB *pcb = node->pcb;
    // change priority
    pcb->process_priority = priority;
    // if in ready queue, move
    if (node->pcb->status == READY) {
        k_delete_in_processlists(PL, pid);
        k_add_tail(PL->list_pointer[priority + 1], pcb);
    }
    k_leave_last_protected_mode();
    return SUCCESS;
}

void p_exit(void) {
    // pcb->process_state = EXITED;
    k_enter_protected_mode();
    k_terminate(current_pcb, false);
    k_leave_last_protected_mode();
    if (cpu_mode == USER_MODE) {
        setcontext(&schedulerContext);
    }
}

// Process Statuses
bool W_WIFEXITED(int wstatus) { return (wstatus == EXITED) ? true : false; }

bool W_WIFSTOPPED(int wstatus) { return (wstatus == STOPPED) ? true : false; }

bool W_WIFSIGNALED(int wstatus) { return (wstatus == SIGNALED) ? true : false; }

// protection
void k_enter_protected_mode() {
    // protect some procedure e.g. manipulating plist from being interupted
    // 0.check validity
    if (protection_level < 0 || protection_level > MAX_PROTECTION_LAYERS - 1) {
        // todo raise error
        return;
    }
    // 1. set supervision bit
    cpu_mode_stack[protection_level] = cpu_mode;
    cpu_mode = KERNEL_MODE;
    // 2. block signals
    if ((sigfillset(&protect_sigmask) == -1)) {
        perror("Failed to initialize the signal mask");
        exit(EXIT_FAILURE);
    }
    if (sigprocmask(SIG_BLOCK, &protect_sigmask, sigmask_stack + protection_level) == -1) {
        perror("Unable to block signal");
        exit(EXIT_FAILURE);
    }
    protection_level++;
    // if (current_pcb != NULL) {
    //     printf("Entered a protection layer in [pid=%d]:%s:\n", current_pcb->pid, current_pcb->process_name);
    // } else {
    //     printf("Entered a protection layer:\n");
    // }
    return;
}

void k_leave_last_protected_mode() {
    // recover the state before enter protected mode
    // only leave when are in the last level
    if (protection_level - 1 < 0 || protection_level > MAX_PROTECTION_LAYERS) {
        return;
    }
    protection_level--;
    cpu_mode = cpu_mode_stack[protection_level];
    if (sigprocmask(SIG_SETMASK, sigmask_stack + protection_level, NULL) == -1) {
        perror("Unable to reset signal mask");
        exit(EXIT_FAILURE);
    }

    if (current_pcb != NULL) {
        // printf("Leave a protection layer in [pid=%d]:%s:\n", current_pcb->pid, current_pcb->process_name);
    } else {
        // printf("Leave a protection layer:\n");
    }
    // printf("\tAfter leave: SP=%d UK=%d\n", protection_level, cpu_mode);
    return;
}

void p_logout() {
    k_enter_protected_mode();
    setcontext(&mainContext);
}

PCB *get_current_PCB() { return current_pcb; }

PCB *get_fg_PCB() { return fg_pcb; }

processlists *get_scheduler() { return PL; }

// list all processes on PennOS in order
// display pid, ppid, and priority.
void p_ps() {
    // find the maximum pid in the process pool
    int max_pid = k_find_max_pid_in_process_pool(PL);
    // todo catch error
    if (max_pid < 0) {
        return;
    }
    printf("PID  PPID  PRI   STAT  CMD\n");
    const char* const states[] = {"Z", "T", "S", "B", "R", "Z"};
    // traverse all the pid
    for (int pid = 0; pid <= max_pid; pid++) {
        pnode* node = k_get_node_by_pid_in_multiple_lists(PL, pid);
        if (node != NULL) {
            int ppid = node->pcb->ppid;
            int priority = node->pcb->process_priority;
            char* cmd = node->pcb->process_name;
            if (priority != -1) {
                printf("%d    %d     %d     %s     %s\n", pid, ppid, priority,
                         states[node->pcb->status], cmd);
            } else {
                printf("%d    %d     %d    %s     %s\n", pid, ppid, priority,
                         states[node->pcb->status], cmd);
            }
        }
    }
}
