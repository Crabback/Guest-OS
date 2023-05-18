#include <sys/types.h>
// max lenght of the log file
#define MAX_LOG_FILENAME 1024

void log_process_change(char* action, pid_t pid, int priority,char* process_name);
void log_schedule_event(pid_t pid, int priority, char* process_name);
void init_log();
void close_log();
void logger_flush() ;