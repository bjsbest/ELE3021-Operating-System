//
// User-space helpers for kernel threads.
//
// thread_create() and thread_join() are thin wrappers over the clone()
// and join() system calls.  They mirror the spirit of pthread_create()
// and pthread_join():

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"
#include "user/uthread.h"

int
thread_create(void (*fn)(void *), void *arg, int n_pages)
{
  void* stack_base = malloc(n_pages * PGSIZE);
  if(stack_base == 0) return -1; // failed to malloc

  uint64 stack_top = (uint64)stack_base + (n_pages * PGSIZE);

  int pid = clone(fn, arg, (void*)stack_top, n_pages, CLONE_VM);
  if(pid < 0){
    free(stack_base);
    return -1; // fail to clone
  }

  return pid; // success to clone!
}

int
thread_join(void)
{
  void* user_stack_base = 0;
  int pid = join(&user_stack_base); // dead thread's pid
  if(pid < 0) return -1; // fail

  if(user_stack_base != 0){
    free(user_stack_base);
  }

  return pid;
} 
