
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/riscv.h"
#include "user/uthread.h"

int shared_value = 0;
volatile int thread_exec_failed = 0;

//DO NOTHING.
void
dummy(void *arg) {
  exit(0);
}

//TEST FOR SHARED MEMORY BETWEEN THREADS.
void
th_add(void *arg){
  int n = (int)(uint64)arg;
  shared_value += n;
  exit(0);
}

//TEST FOR KILLING GROUP LEADER.
// DO NOTHING. JUST WAIT UNTIL KILLED BY GROUP LEADER.
void
th_nothing(void *arg){
  while(1);
}

//TEST FOR EXITING GROUP LEADER.
// pause and exit.
void
th_exit(void *arg){
  pause(100);
  exit(0);
}

void
th_exec(void *arg){
  char *args[] = { "echo", "[FAIL] THREAD EXEC EXECUTED.", 0 };
  exec("echo", args);
  thread_exec_failed = 1; // if exec fails, this line will be executed.
  exit(0);
}

void
th_concurrent_sbrk(void *arg){
  // each thread expands and shrinks the heap 100 times.
  for(int i = 0; i < 100; i++){
    void *mem = sbrk(PGSIZE);
    if(mem == (void *)-1){
      printf("[FAIL] CONCURRENT SBRK ALLOCATION FAILED.\n");
      exit(1);
    }
    
  
    char *ptr = (char *)mem;
    ptr[0] = 'X'; 
    ptr[4095] = 'Y';

    // shrink the heap back.
    sbrk(-PGSIZE);
  }
  exit(0);
}

void
test1(){
  printf("\n=== TEST 1. THREAD CREATION/JOIN TEST === \n");
  int created = thread_create(dummy, (void *)0, 1);
  int joined = thread_join();

  printf("THREAD CREATED PID : %d\n", created);
  printf("THREAD JOINED PID : %d\n", joined);

  if(created == joined && created != -1){
    printf("TEST 1 PASS\n");
  }else{
    printf("TEST 1 FAILED\n");
  }
}

void
test2(){
  printf("\n=== TEST 2. MAXIMUM THREAD TEST === \n");
  for(int i = 0; i < 7; i++){
    thread_create(dummy, (void *)0, 1);
  }

  int fail_tid = thread_create(dummy, (void *)0, 1); //MUST BE FAILED.
  if(fail_tid >= 0){
    printf("TEST 2 FAILED.\n");
  }else{
    printf("TEST 2 PASS \n");
  }

  for(int i = 0; i < 7; i++){
    thread_join();
  }
}

void
test3(){
  printf("\n=== TEST 3. SHARED MEMORY TEST === \n");
  shared_value = 0;
  for(int i = 0; i < 5; i++){
    int tid = thread_create(th_add, (void *)10, 1);
    if(tid < 0){
      printf("THREAD CREATION FAILED AT ITERATION %d\n", i);
    }
  }

  for(int i = 0; i < 5; i++){
    thread_join();
  }

  if(shared_value != 50){
    printf("TEST 3 FAILED. EXPECTED VALUE : 50, ACTUAL VALUE : %d\n", shared_value);
    exit(1);
  }else{
    printf("TEST 3 PASS. SHARED VALUE : %d\n", shared_value);
  }
  
}

void
test4(){
  printf("\n=== TEST 4. THREAD CREATION STRESS TEST === \n");
  printf("CREATING THREADS...\n");
  unsigned char failed = 0;
  for(int i = 1; i < 1001; i++){
    int tid = thread_create(dummy, (void *)0, 1);
    if(tid < 0){
      failed = 1;
      break;
    }
    thread_join();
    if(i % 100 == 0){
      printf("%d THREADS CREATED AND JOINED\n", i);
    }
  }

  if(!failed){
    printf("TEST 4 PASS.\n");
  }else{
    printf("TEST 4 FAILED.\n");
  }
}

void
test5(){
  printf("\n=== TEST 5. GROUP LEADER EXIT TEST === \n");
  int pid = fork();
  if(pid < 0){
    printf("FORK FAILED\n");
    exit(1);
  }else if(pid == 0){
    for(int i = 0; i < 7; i++) thread_create(th_nothing, (void *)0, 1);
    exit(0);
  }else{
    wait(0);
    
    // debug
    volatile int t=0;
    for(t=0; t<2000000000; t++);

    int create_count = 0;
    for(int i = 0; i < 70; i++){
      int child_pid = fork();
      if(child_pid < 0){
        break;
      }else if(child_pid == 0){
        exit(0);
      }else{
        create_count++;
      }
    }

    for(int i = 0; i < create_count; i++){
      wait(0);
    }

    if(create_count < 60) printf("TEST 5 FAILED. create_count : %d\n", create_count);
    else printf("TEST 5 PASS. create_count : %d\n", create_count);
  }
}

void
test6(){
  printf("\n=== TEST 6. THREAD GROUP EXEC TEST === \n");
  int pid = fork();
  if(pid < 0){
    printf("FORK FAILED\n");
    exit(1);
  }else if(pid == 0){
    for(int i = 0; i < 6; i++) thread_create(th_nothing, (void *)0, 1);
    thread_create(th_exec, (void *)0, 1); //must be failed.
    pause(20);
    if(thread_exec_failed){
      printf("[PASS] THREAD EXEC FAILED AS EXPECTED\n");
    }
    exec("echo", (char *[]){ "echo", "[PASS] LEADER EXEC PASSED.", 0});
    printf("[FAIL] LEADER EXEC FAILED.\n");
    exit(1);
  }else{
    wait(0);
    
    int create_count = 0;
    for(int i = 0; i < 70; i++){
      int child_pid = fork();
      if(child_pid < 0){
        break;
      }else if(child_pid == 0){
        exit(0);
      }else{
        create_count++;
      }
    }

    for(int i = 0; i < create_count; i++){
      wait(0);
    }

    if(create_count < 60) printf("[FAIL] EXEC RESOURCE MANAGEMENT\n");
    else printf("[PASS] EXEC RESOURCE MANAGEMENT\n");
  }
}

void
test7(){
  printf("\n=== TEST 7. KILL TEST === \n");
  int pid = fork();
  if(pid < 0){
    printf("FORK FAILED\n");
    exit(1);
  }else if(pid == 0){
    for(int i = 0; i < 6; i++) thread_create(th_nothing, (void *)0, 1);
    int tid = thread_create(th_nothing, (void *)0, 1);
    kill(tid); // kill one of the sibling threads.

    printf("[FAIL] KILL TEST FAILED\n"); // this should not be printed.
    exit(1);
  }else{
    wait(0);

    int create_count = 0;
    for(int i = 0; i < 70; i++){
      int child_pid = fork();
      if(child_pid < 0){
        break;
      }else if(child_pid == 0){
        exit(0);
      }else{
        create_count++;
      }

      // printf("create_count : %d\n", create_count);
    }

    for(int i = 0; i < create_count; i++){
      wait(0);
    }

    if(create_count > 60) printf("[PASS] KILL TEST PASSED\n");
  }
}

void
test8(){
  printf("\n=== TEST 8. WAIT TEST === \n");
  int tid = -1;
  int pid = fork();
  if(pid < 0){
    printf("FORK FAILED\n");
    exit(1);
  }else if(pid == 0){
    tid = thread_create(th_exit, (void *)0, 1); // create a thread which will exit soon.
    pause(10); // wait until the thread exits.
    exit(0);
  }else{
    int result = wait(0);
    if(result < 0 || result == tid){
      printf("[FAIL] WAIT TEST FAILED\n");
    }else if(result == pid){
      printf("[PASS] SUCCESSFULLY WAITED FOR GROUP LEADER PROCESS\n");
    }else{
      printf("[FAIL] WAIT TEST FAILED. WAIT RETURNED UNKNOWN PID : %d\n", result);
    } 
  }
}

void
test9(){
  printf("\n=== TEST 9. CONCURRENCY STRESS TEST === \n");
  
  int created = 0;

  for(int i = 0; i < 5; i++){
    int tid = thread_create(th_concurrent_sbrk, (void *)0, 1);
    if(tid > 0){
      created++;
    }else{
      printf("THREAD CREATION FAILED AT ITERATION %d\n", i);
    }
  }

  for(int i = 0; i < created; i++){
    thread_join();
  }

  if(created == 5){
    printf("[PASS] CONCURRENCY SBRK TEST PASSED. NO KERNEL PANIC.\n");
  }else{
    printf("[FAIL] FAILED TO CREATE TARGET NUMBER OF THREADS.\n");
  }
}

int
main(void){
  test1();
  test2();
  test3();
  test4();

  volatile int t=0;
  for(t=0; t<1000000000; t++);

  test5();
  test6();
  test7();
  test8();
  test9();
  // exec("usertests", (char *[]){ "usertests", 0});
  return 0;
}
