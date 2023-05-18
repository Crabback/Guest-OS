#include "../macros.h"
#include <stdbool.h>
#include <sys/types.h>
#include <ucontext.h>

enum STATE // All the states of a process
{
    EXITED = 0,
    STOPPED = 1,
    BLOCKED = 2,
    READY = 3,
    SIGNALED = 4,
    ORPHAN = 5,
    RUNNING = 6,
    WAITING = 7,
    ZOMBIED = 8
};

// Priorities, there are three priorities in this OS
enum Priority {
    HIGH = -1,
    MED = 0,
    LOW = 1,
};

// define PCB
typedef struct PCB {
    int signal;
    pid_t pid;
    pid_t ppid; // parent_pid
    // int status;
    int num_children;
    unsigned int remaining_ticks;
    pid_t children[MAX_PROCESSES];
    char process_name[MAX_PROCESSES];
    enum Priority process_priority; //-1,0,1
    enum STATE status;              //
    // pointer to context
    ucontext_t context;
    ucontext_t *ucp;
    // fd table
    int open_fds[MAX_FILES];
    int stdin;
    int stdout;
    int pid_waitfor;  // this process is in waiting list
    int sleep_remain; // this process is in speical sleep list, how long remained to sleep
} PCB;

typedef struct pNode {
    PCB *pcb;           ///< all info are stored in PCB, here only maintain a ptr
    struct pNode *next; ///< next node
    struct pNode *prev; ///< prev node
} pnode;

typedef struct processLists {
    pnode *ready_queue_high;
    pnode *ready_queue_med;
    pnode *ready_queue_low;
    pnode *blocked_queue;
    pnode *stopped_queue;
    pnode *zombie_queue;
    pnode *list_pointer[TOTAL_NUM_LIST];
    pnode *waiting_list; // a process is blocked by waitpid
    pnode *sleep_list;   // a process is blocked by sleep
} processlists;

/* ------------------------------------------------------------------------
------------------------- User-level Functions -----------------------
------------------------------------------------------------------------*/
pid_t p_spawn(void (*func)(), char *argv[], int fd0, int fd1);
pid_t p_waitpid(pid_t pid, int *wstatus, bool nohang);
int p_kill(pid_t pid, int sig);
void p_sleep(unsigned int ticks);
int p_nice(pid_t pid, int priority);
void p_exit(void);
void p_logout();
void p_ps();

/* ------------------------------------------------------------------------
------------------------- Kernel-level Functions -----------------------
------------------------------------------------------------------------*/
PCB *k_process_create(PCB *parent);
int k_process_kill(PCB *process, int signal);
int k_process_cleanup(PCB *process);

/* ------------------------------------------------------------------------
------------------------- Helper Functions -----------------------
------------------------------------------------------------------------*/
void sig_handler(int signo);
PCB *get_current_PCB();
PCB *get_fg_PCB();
processlists *get_scheduler();
pnode *get_queue_head(pnode *s);
// static void alarm_handler(int signum);
// static void k_set_alarm_handler(void (*handler)());
// static void k_set_timer(void);
void k_sig_reg();
void k_unblock_wait(processlists *pl, PCB *pcb);
int k_terminate(PCB *pcb, bool signaled);
void kernel_thread_scheduler();
void kernel_thread_returner();
int k_process_control_initiate();
int k_process_control_start();
void k_enter_protected_mode();
void k_leave_last_protected_mode();

bool W_WIFEXITED(int wstatus);
bool W_WIFSTOPPED(int wstatus);
bool W_WIFSIGNALED(int wstatus);