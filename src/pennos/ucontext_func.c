#include "ucontext_func.h"
#include "../macros.h"
#include <signal.h>   // sigaction, sigemptyset, sigfillset, signal
#include <stdio.h>    // dprintf, fputs, perror
#include <stdlib.h>   // malloc, free
#include <sys/time.h> // setitimer
#include <ucontext.h> // getcontext, makecontext, setcontext, swapcontext
#include <unistd.h>   // read, usleep, write
#include <valgrind/valgrind.h>

int k_set_stack(stack_t *stack) {
    void *sp = malloc(SIGSTKSZ);
    if (sp == NULL) {
        perror("malloc");
        return FAILURE;
    }
    VALGRIND_STACK_REGISTER(sp, sp + SIGSTKSZ);
    *stack = (stack_t){.ss_sp = sp, .ss_size = SIGSTKSZ};
    return SUCCESS;
}

int k_make_context(ucontext_t *ucp, ucontext_t *next, void (*func)(), char **argv, int mode) {
    getcontext(ucp);
    if (k_set_stack(&ucp->uc_stack)) {
        return FAILURE;
    }
    ucp->uc_link = next; // func == cat ? &mainContext : NULL;
    if (mode == KERNEL_THREAD) {
        if (sigfillset(&ucp->uc_sigmask)) {
            perror("sigfillset");
            return FAILURE;
        }
        makecontext(ucp, func, 0);
    } else {
        // all user apps should receive signals
        if (sigemptyset(&ucp->uc_sigmask)) {
            perror("sigemptyset");
            return FAILURE;
        }
        makecontext(ucp, func, 1, argv);
    }
    return SUCCESS;
}
