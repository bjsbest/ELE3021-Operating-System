#include "kernel/types.h"
#include "user.h"

int main(int argc, char *argv[]){
  for (int nice = 2; nice >= -3; nice--){
    int pid = fork();
    if (pid < 0){
      printf("fork failed\n");
      exit(1);
    }

    if (pid == 0){
      set_nice(nice);
      for (volatile int i = 0; i < 200000000; i++);
      printf("pid: %d, nice: %d\n", getpid(), nice);
      exit(0);
    }
  }

  for (int i = 0; i < 6; i++){
    wait(0);
  }

  return 0;
}

