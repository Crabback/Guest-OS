#include <ucontext.h>
int k_set_stack(stack_t *stack);
int k_make_context(ucontext_t *ucp, ucontext_t *next, void (*func)(), char** argv, int mode);