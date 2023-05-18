// #include "process_control.h"
pnode * k_create_list_node(PCB* pcb);
pnode * k_add_head(pnode* head, PCB* pcb);
pnode * k_add_tail(pnode* head, PCB* pcb);
pnode * k_delete(pnode* head, PCB* pcb);
pnode * k_get_node_by_pid_in_single_list(pnode* head, pid_t pid);
pnode * k_get_node_by_ppid_in_single_list(pnode* head, pid_t pid);
pnode * k_get_node_by_pid_in_multiple_lists(processlists* pl, pid_t pid);
processlists * k_create_process_lists();
pid_t k_find_available_pid(processlists* pl);
void k_delete_in_processlists(processlists *pl, pid_t pid);
int k_find_max_pid_in_process_pool(processlists *pl);