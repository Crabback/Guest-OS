#define SUCCESS 0
#define FAILURE -1
#define MAX_PROCESSES 100

// //STDIN_FILENO STDOUT_FILENO
// #define STDIN 0
// #define STDOUT 1

#define NUM_PRIORITY_LEVELS 3
#define TOTAL_NUM_LIST 6
#define MAX_LENGTH 1024
#define MAX_ARGS 64
#define MAX_PROTECTION_LAYERS 1024
//the length of fd arrays in each pcb
#define MAX_FILES 512

#define SCHEDULER_HIGH 9
#define SCHEDULER_MED 6
#define SCHEDULER_LOW 4

#define KERNEL_MODE 0
#define USER_MODE 1

//make ucontext
#define KERNEL_THREAD 0
#define USER_THREAD 1

// Singals
#define S_SIGSTP 0  ///< stop signal
#define S_SIGCONT 1  ///< continue signal
#define S_SIGTERM 2  ///< terminate signal
#define S_SIGINT 3  ///< ctrl-C signal

#define NO_PERM 0
#define EXEC_PERM 1
#define WRITE_PERM 2
#define EXEC_WRITE_PERM 3
#define READ_PERM 4
#define EXEC_READ_PERM 5
#define READWRITE_PERM 6
#define EXEC_READ_WRITE_PERM 7

#define READ_PERMISSION 0444
#define WRITE_PERMISSION 0666

#define F_WRITE 0
#define F_READ 1
#define F_APPEND 2