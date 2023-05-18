#include <signal.h>   // sigaction, sigemptyset, sigfillset, signal
#include <stdio.h>    // dprintf, fputs, perror
#include <stdlib.h>   // malloc, free
#include <sys/time.h> // setitimer
#include <ucontext.h> // getcontext, makecontext, setcontext, swapcontext
#include <unistd.h>   // read, usleep, write
#include <valgrind/valgrind.h>

#include "../macros.h"
#include "process_control.h"
#include "process_list.h"
// static const int centisecond = 10000; // 10 milliseconds
int sched_list[SCHEDULER_HIGH + SCHEDULER_MED + SCHEDULER_LOW];
int sched_tail = -1;
int sched_start = 0;

int k_schedule(processlists *plists) {
    pnode *head;
    int ready_queue_id = 0;
    if (sched_tail == -1) {
        // the first scheduling, directly find the first ready queue
        for (int i = 0; i < NUM_PRIORITY_LEVELS; i++) {
            head = plists->list_pointer[i];
            if (head->next != head) {
                sched_tail = 0;
                sched_list[sched_tail] = i;
                ready_queue_id = i;
                break;
            }
        }
    } else {

        // 1. count the running time in a short window
        int run_count[NUM_PRIORITY_LEVELS];
        for (int i = 0; i < NUM_PRIORITY_LEVELS; i++) {
            int count = 0;
            int index = sched_start;
            while (1) {
                if (sched_list[index] == i) {
                    count++;
                }
                if (index == sched_tail) {
                    break;
                }
                index = (index + 1) % (SCHEDULER_HIGH + SCHEDULER_MED + SCHEDULER_LOW);
            }
            run_count[i] = count;
        }

        int current_queue[2];
        int current_queue_id = 0;
        int max_gap = -(SCHEDULER_HIGH + SCHEDULER_MED + SCHEDULER_LOW);
        int standard_count[3] = {SCHEDULER_HIGH, SCHEDULER_MED, SCHEDULER_LOW};
        // count current queues
        int current_queue_num = 0;
        for (int i = 0; i < NUM_PRIORITY_LEVELS; i++) {
            pnode *head = plists->list_pointer[i];
            if (head->next != head) {
                current_queue_num++;
            }
        }
        switch (current_queue_num) {
        case 1:
            // only one ready queue
            for (int i = 0; i < NUM_PRIORITY_LEVELS; i++) {
                head = plists->list_pointer[i];
                if (head->next != head) {
                    ready_queue_id = i;
                    break;
                }
            }
            break;
        case 2:
            //  two ready queue queues
            for (int i = 0; i < NUM_PRIORITY_LEVELS; i++) {
                pnode *queue_head = plists->list_pointer[i];
                if (queue_head->next != queue_head) {
                    current_queue[current_queue_id] = i;
                    current_queue_id++;
                }
            }
            if (run_count[current_queue[1]] == 0) {
                head = plists->list_pointer[current_queue[1]];
                ready_queue_id = current_queue[1];
            } else {
                // test the ratio
                double ratio = (double)run_count[current_queue[0]] / (double)run_count[current_queue[1]];
                double standard_ratio = (double)standard_count[current_queue[0]] / (double)standard_count[current_queue[1]];
                if (ratio > standard_ratio) {
                    // C1/C2 > S1/S2, C2 need more turns
                    ready_queue_id = current_queue[1];
                    head = plists->list_pointer[current_queue[1]];
                } else {
                    // C1/C2 < S1/S2, C1 need more turns
                    ready_queue_id = current_queue[0];
                    head = plists->list_pointer[current_queue[0]];
                }
            }
            break;
        case 3:
            // we selected the queue with biggest gap
            for (int i = 0; i < NUM_PRIORITY_LEVELS; i++) {
                if (max_gap < standard_count[i] - run_count[i]) {
                    max_gap = standard_count[i] - run_count[i];
                    ready_queue_id = i;
                    head = plists->list_pointer[i];
                }
            }
            break;
        default:
            head = plists->ready_queue_high;
            break;
        }

        // update the time window
        sched_tail = (sched_tail + 1) % (SCHEDULER_HIGH + SCHEDULER_MED + SCHEDULER_LOW);
        sched_list[sched_tail] = ready_queue_id;
        if (sched_start == sched_tail) {
            sched_start = (sched_start + 1) % (SCHEDULER_HIGH + SCHEDULER_MED + SCHEDULER_LOW);
        }
    }
    // return head;
    return ready_queue_id;
}