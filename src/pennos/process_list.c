
#include <stdio.h>
#include <stdlib.h>
#include "process_control.h"
#include "../macros.h"

// single process list
pnode *k_create_list_node(PCB *pcb) {
    pnode *node = malloc(sizeof(pnode));
    if (node == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    node->pcb = pcb;
    node->next = node;
    node->prev = node;
    return node;
}

void k_add_head(pnode *head, PCB *pcb) {
    pnode *node = k_create_list_node(pcb);
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
    node->prev = head;
    return;
}

void k_add_tail(pnode *head, PCB *pcb) {
    pnode *node = k_create_list_node(pcb);
    pnode *tail = head->prev;
    tail->next = node;
    node->prev = tail;
    head->prev = node;
    node->next = head;
    return;
}

pnode *k_get_node_by_pid_in_single_list(pnode *head, pid_t pid) {
    pnode *node = head->next;
    while (node !=head) {
        if (node->pcb->pid == pid) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

void k_delete(pnode *head, PCB *pcb) {
    pnode * deletenode = k_get_node_by_pid_in_single_list(head, pcb->pid);
    if (deletenode != NULL) {
        pnode *prevnode = deletenode->prev;
        pnode *nextnode = deletenode->next;
        prevnode->next = nextnode;
        nextnode->prev = prevnode;
        free(deletenode);
    }
    return;
}

pnode *k_get_node_by_ppid_in_single_list(pnode *head, pid_t pid) {
    pnode *node = head->next;
    while (node !=head) {
        if (node->pcb->ppid == pid) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

// process lists

processlists *k_create_process_lists() {
    processlists *pl;
    pl = malloc(sizeof(processlists));
    if (pl == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    pl->ready_queue_high = k_create_list_node(NULL);
    pl->list_pointer[0] = pl->ready_queue_high;
    pl->ready_queue_med = k_create_list_node(NULL);
    pl->list_pointer[1] = pl->ready_queue_med;
    pl->ready_queue_low = k_create_list_node(NULL);
    pl->list_pointer[2] = pl->ready_queue_low;
    pl->blocked_queue = k_create_list_node(NULL);
    pl->list_pointer[3] = pl->blocked_queue;
    pl->stopped_queue = k_create_list_node(NULL);
    pl->list_pointer[4] = pl->stopped_queue;
    pl->zombie_queue = k_create_list_node(NULL);
    pl->list_pointer[5] = pl->zombie_queue;
    pl->sleep_list = k_create_list_node(NULL);
    pl->waiting_list = k_create_list_node(NULL);
    return pl;
}

pnode *k_get_node_by_pid_in_multiple_lists(processlists *pl, pid_t pid) {
    pnode *node;
    for (int i = 0; i < TOTAL_NUM_LIST; i++) {
        node = k_get_node_by_pid_in_single_list(pl->list_pointer[i], pid);
        if (node != NULL) {
            return node;
        }
    }
    return node;
}

pid_t k_find_available_pid(processlists *pl) {
    pid_t pid = 1;
    while (k_get_node_by_pid_in_multiple_lists(pl, pid) != NULL) {
        pid++;
    }
    return pid;
}

void k_delete_in_processlists(processlists *pl, pid_t pid) {
    pnode *node;
    for (int i = 0; i < TOTAL_NUM_LIST; i++) {
        node = k_get_node_by_pid_in_single_list(pl->list_pointer[i], pid);
        if (node != NULL) {
            k_delete(pl->list_pointer[i], node->pcb);
        }
    }
}

int k_find_max_pid_in_process_pool(processlists *pl) {
    int max = -1;
    for (int i = 0; i < TOTAL_NUM_LIST; i++) {
        pnode* head = pl->list_pointer[i];
        pnode* node = head->next;
        while (node != head) {
            int pid = node->pcb->pid;
            max = (pid > max) ? pid : max;
            node = node->next;
        }
    }
    return max;
}