#include "logger.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "process_control.h"
#include "process_list.h"

extern int cpu_time;
extern int cpu_mode;
FILE *log_fp;
char *default_output_file = "../log/pennos_log";

void getDateTime(char *t) {
    time_t rawtime;
    struct tm *ltime;
    time(&rawtime);
    ltime = localtime(&rawtime);
    strftime(t, 20, "_%Y-%m-%d_%H:%M", ltime);
}

void init_log() {
    // get current time and prepare name
    char log_name[MAX_LOG_FILENAME];
    char timestamp[MAX_LOG_FILENAME];
    getDateTime(timestamp);
    strcpy(log_name, default_output_file);
    strcat(log_name, timestamp);
    strcat(log_name, ".txt");
    log_fp = fopen(log_name, "w+");
    if (log_fp == NULL) {
        perror("Error opening log file");
        return;
    }
    time_t t;
    struct tm *timeinfo = NULL;
    time(&t);
    timeinfo = localtime(&t);
    fprintf(log_fp, "PennOS Log\nRunning at %s", asctime(timeinfo));
    return;
}

void close_log() { fclose(log_fp); }

void log_schedule_event(pid_t pid, int priority, char *process_name) {
    fprintf(log_fp, "[%-5d]\t%s\t%-3d\t%-2d\t%s\n", cpu_time, "SCHEDULE", pid, priority, process_name);
}

void log_process_change(char *action, pid_t pid, int priority, char *process_name) {
    fprintf(log_fp, "[%-5d]\t%s\t%-3d\t%-2d\t%s\n", cpu_time, action, pid, priority, process_name);
    return;
}

void logger_flush() { fflush(log_fp); }