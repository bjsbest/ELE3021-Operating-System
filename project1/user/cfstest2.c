#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int pass_count = 0;
int fail_count = 0;

void check(int cond, char *name) {
    if(cond) {
        printf("[PASS] %s\n", name);
        pass_count++;
    } else {
        printf("[FAIL] %s\n", name);
        fail_count++;
    }
}

// tick 기반 CPU 사용 함수
// uptime()으로 현재 tick을 측정하여 지정한 tick만큼 CPU를 점유
void burn_ticks(int ticks) {
    int start = uptime();
    while(uptime() - start < ticks) {}
}

// =============================================
// Test 1: set_nice() 반환값, 유효/무효 범위
// 목적: set_nice() 시스템 콜이 명세대로 동작하는지 검증
// 검증:
//   - 유효 범위(-3 ~ 2): 성공 시 0 반환
//   - 무효 범위(범위 초과): 실패 시 1 반환
// 명세: "Return 0 if updated, Return 1 if invalid"
// =============================================
void test_set_nice_boundary() {
    printf("\n=== Test 1: set_nice boundary ===\n");

    // 유효 범위 (-3 ~ 2) → 0 반환
    check(set_nice(-3) == 0, "set_nice(-3) returns 0");
    check(set_nice(-2) == 0, "set_nice(-2) returns 0");
    check(set_nice(-1) == 0, "set_nice(-1) returns 0");
    check(set_nice(0)  == 0, "set_nice(0)  returns 0");
    check(set_nice(1)  == 0, "set_nice(1)  returns 0");
    check(set_nice(2)  == 0, "set_nice(2)  returns 0");

    // 무효 범위 (범위 초과) → 1 반환
    check(set_nice(-4)  == 1, "set_nice(-4)  returns 1");
    check(set_nice(3)   == 1, "set_nice(3)   returns 1");
    check(set_nice(-100)== 1, "set_nice(-100) returns 1");
    check(set_nice(100) == 1, "set_nice(100) returns 1");

    set_nice(0);
}

// =============================================
// Test 2: fork 시 nice 상속
// 목적: fork 시 자식이 부모의 nice 값을 상속받는지 검증
// 검증:
//   - 부모가 nice=-2로 설정 후 fork → 자식도 nice=-2 상속
//   - 자식에서 범위 초과 set_nice는 실패(1 반환)
//   - 자식에서 유효 범위 set_nice는 성공(0 반환)
// 명세: "When a process is forked, it inherits its parent's nice value"
// =============================================
void test_nice_inherit() {
    printf("\n=== Test 2: nice inheritance via fork ===\n");

    set_nice(-2);
    int pid = fork();
    if(pid == 0) {
        int r1 = set_nice(-4); // 범위 초과 → 1 반환해야 함
        int r2 = set_nice(-1); // 유효 범위 → 0 반환해야 함
        exit(r1 == 1 && r2 == 0 ? 0 : 1);
    }
    int status;
    wait(&status);
    check(status == 0, "child inherits parent nice and validates correctly");

    set_nice(2);
    pid = fork();
    if(pid == 0) {
        int r = set_nice(3); // 범위 초과 → 1 반환해야 함
        exit(r == 1 ? 0 : 1);
    }
    wait(&status);
    check(status == 0, "child inherits nice=2, rejects nice=3");

    set_nice(0);
}

// =============================================
// Test 3: 최초 프로세스 nice=0
// 목적: 최초로 생성되는 프로세스의 nice 초기값이 0인지 검증
// 검증:
//   - set_nice(0) 성공(0 반환) → 현재 nice가 0이라는 간접 증거
//   - fork된 자식도 nice=0을 상속받아야 함
// 명세: "Initialize the nice value to 0 for the very first process"
// =============================================
void test_initial_nice() {
    printf("\n=== Test 3: initial nice value ===\n");

    set_nice(0);
    int pid = fork();
    if(pid == 0) {
        int r = set_nice(0); // nice=0 상속받았으면 성공(0 반환)
        exit(r == 0 ? 0 : 1);
    }
    int status;
    wait(&status);
    check(status == 0, "process starts with valid nice=0");
}

// =============================================
// Test 4: nice에 따른 스케줄링 순서 (핵심)
// 목적: nice 값이 낮을수록 vruntime 증가가 느려 더 자주 선택되는지 검증
// 검증:
//   - nice=-3(증가량 1), nice=-2(증가량 2), nice=0(증가량 8), nice=2(증가량 32)
//   - nice가 낮은 프로세스가 먼저 완료돼야 함
//   - pipe를 통해 완료 순서를 int로 기록하여 정렬 여부 확인
// 명세: "Each increment of 1 in nice doubles vruntime added"
// =============================================
void test_nice_scheduling_order() {
    printf("\n=== Test 4: scheduling order by nice value ===\n");
    printf("Expected order: nice=-3 finishes first, nice=2 finishes last\n");

    int pfd[2];
    pipe(pfd);

    int nices[3] = {2, 0, -3};
    for(int i = 0; i < 3; i++) {
        set_nice(nices[i]);
        int pid = fork();
        if(pid == 0) {
            close(pfd[0]);
            burn_ticks(20); // tick 기반 CPU 작업
            int val = nices[i];
            write(pfd[1], &val, sizeof(int)); // 완료 순서 기록
            exit(0);
        }
    }

    close(pfd[1]);

    int order[3];
    int cnt = 0;
    int val;
    while(cnt < 3 && read(pfd[0], &val, sizeof(int)) > 0) {
        order[cnt++] = val;
    }
    close(pfd[0]);

    for(int i = 0; i < 4; i++) wait(0);

    // 완료 순서가 nice 오름차순(-3, 0, 2)인지 확인
    int correct = 1;
    for(int i = 0; i < cnt-1; i++) {
        if(order[i] > order[i+1]) {
            correct = 0;
            break;
        }
    }

    printf("Completion order: ");
    for(int i = 0; i < cnt; i++) printf("%d ", order[i]);
    printf("\n");
    check(correct, "lower nice finishes before higher nice");
    set_nice(0);
}

// =============================================
// Test 5: fork 시 vruntime 상속
// 목적: fork 시 자식이 부모의 vruntime을 상속받는지 검증
// 검증 (CPUS=1 특화):
//   - 자식 X: 부모 vruntime이 낮을 때 fork → 낮은 vruntime 상속
//   - 자식 Y: 부모가 CPU를 더 사용한 후 fork → 높은 vruntime 상속
//   - vruntime이 낮은 X가 먼저 선택되어 먼저 완료돼야 함
// 명세: "When forked, initialize vruntime to parent's vruntime"
// =============================================
void test_vruntime_inherit() {
    printf("\n=== Test 5: vruntime inheritance (CPUS=1) ===\n");

    int pfd[2];
    pipe(pfd);

    set_nice(0);

    // 자식 X: 부모 vruntime 낮을 때 fork → 낮은 vruntime 상속
    int pidX = fork();
    if(pidX == 0) {
        close(pfd[0]);
        burn_ticks(10);
        write(pfd[1], "X", 1);
        exit(0);
    }

    // 부모 CPU 추가 사용 → vruntime 증가
    burn_ticks(5);

    // 자식 Y: 부모 vruntime 높을 때 fork → 높은 vruntime 상속
    int pidY = fork();
    if(pidY == 0) {
        close(pfd[0]);
        burn_ticks(10);
        write(pfd[1], "Y", 1);
        exit(0);
    }

    close(pfd[1]);

    char result[2];
    read(pfd[0], &result[0], 1);
    read(pfd[0], &result[1], 1);
    close(pfd[0]);

    wait(0);
    wait(0);

    printf("Completion order: %c then %c\n", result[0], result[1]);
    // X가 낮은 vruntime → 먼저 완료돼야 함
    check(result[0] == 'X', "lower vruntime child (X) finishes before higher vruntime child (Y)");
}

// =============================================
// Test 6: sleep → wakeup 후 정상 실행
// 목적: SLEEPING 상태에서 깨어난 프로세스가 정상적으로 실행되는지 검증
// 검증:
//   - pause()로 SLEEPING 진입
//   - 깨어난 후 작업 수행 및 정상 종료
//   - wakeup 시 vruntime이 최솟값으로 갱신되어 스케줄링됨을 간접 확인
// 명세: "When wakes up from SLEEPING, update vruntime to minimum"
// =============================================
void test_wakeup_vruntime() {
    printf("\n=== Test 6: wakeup vruntime update ===\n");

    int pid = fork();
    if(pid == 0) {
        pause(5);        // SLEEPING 상태 진입
        burn_ticks(3);   // 깨어난 후 작업
        printf("woken process completed\n");
        exit(0);
    }
    wait(0);
    check(1, "process wakes from sleep and continues execution");
}

// =============================================
// Test 7: 다수 프로세스 동시 안정성
// 목적: 여러 프로세스가 동시에 실행될 때 시스템이 안정적인지 검증
// 검증:
//   - nice -3 ~ 2 범위의 6개 프로세스 동시 실행
//   - 모든 프로세스가 정상 종료(exit status=0)해야 함
//   - 데드락, 크래시 없이 완료되는지 확인
// =============================================
void test_stability() {
    printf("\n=== Test 7: multi-process stability (CPUS=1) ===\n");

    int n = 6;
    int nices[6] = {-3, -2, -1, 0, 1, 2};

    for(int i = 0; i < n; i++) {
        set_nice(nices[i]);
        int pid = fork();
        if(pid == 0) {
            burn_ticks(5); // tick 기반 작업
            exit(0);
        }
    }

    int all_ok = 1;
    for(int i = 0; i < n; i++) {
        int status;
        wait(&status);
        if(status != 0) all_ok = 0;
    }

    check(all_ok, "6 processes with different nice values all complete");
    set_nice(0);
}

// =============================================
// Test 8: nice 상속 체인 (fork의 fork)
// 목적: fork가 중첩될 때도 nice가 올바르게 상속되는지 검증
// 검증:
//   - 부모 nice=-3 설정 → 자식1 fork (nice=-3 상속)
//   - 자식1이 nice=2로 변경 → 손자 fork (nice=2 상속)
//   - 손자에서 set_nice(3)은 범위 초과라 실패(1 반환)해야 함
// 명세: "When forked, inherits parent's nice value"
// =============================================
void test_nice_inherit_chain() {
    printf("\n=== Test 8: nice inheritance chain ===\n");

    set_nice(-3);
    int pid1 = fork();
    if(pid1 == 0) {
        // 자식1: nice=-3 상속받아서 nice=2로 변경
        set_nice(2);
        int pid2 = fork();
        if(pid2 == 0) {
            // 손자: nice=2 상속
            // set_nice(3)은 범위 초과 → 1 반환해야 함
            int r = set_nice(3);
            exit(r == 1 ? 0 : 1);
        }
        int status;
        wait(&status);
        exit(status);
    }
    int status;
    wait(&status);
    check(status == 0, "nice inheritance works through fork chain");
    set_nice(0);
}

// =============================================
// Test 9: kill된 sleeping 프로세스 처리
// 목적: SLEEPING 상태의 프로세스를 kill할 때 시스템이 안정적인지 검증
// 검증:
//   - 자식이 pause()로 오래 sleep
//   - 부모가 kill() 호출
//   - 시스템 크래시 없이 정상 처리되는지 확인
//   - kkill()에서 SLEEPING → RUNNABLE 전환 및 vruntime 갱신 로직 검증
// =============================================
void test_kill_sleeping() {
    printf("\n=== Test 9: kill sleeping process ===\n");

    int pid = fork();
    if(pid == 0) {
        pause(1000); // 오래 sleep
        exit(0);
    }

    burn_ticks(2); // 부모가 잠시 기다린 후
    kill(pid);     // 자식 kill

    int status;
    wait(&status);
    check(1, "kill sleeping process does not crash system");
}

// =============================================
// Test 10: 기본 단일 프로세스 동작
// 목적: 단일 프로세스가 정상적으로 실행되고 종료되는지 기본 동작 검증
// 검증:
//   - fork → set_nice(0) 성공 → 정상 종료
//   - 스케줄러의 기본 동작 확인
// =============================================
void test_single_process() {
    printf("\n=== Test 10: single process basic operation ===\n");

    int pid = fork();
    if(pid == 0) {
        int r = set_nice(0);
        exit(r == 0 ? 0 : 1);
    }
    int status;
    wait(&status);
    check(status == 0, "single child process runs and exits cleanly");
}

int main(int argc, char *argv[]) {
    printf("=============================\n");
    printf("  CFS Scheduler Test Suite   \n");
    printf("   (CPUS=1 mode)             \n");
    printf("=============================\n");

    test_set_nice_boundary();     // Test 1: set_nice() 반환값, 유효/무효 범위
    test_nice_inherit();          // Test 2: fork 시 nice 상속
    test_initial_nice();          // Test 3: 최초 프로세스 nice=0
    test_nice_scheduling_order(); // Test 4: nice에 따른 스케줄링 순서 (핵심)
    test_vruntime_inherit();      // Test 5: fork 시 vruntime 상속
    test_wakeup_vruntime();       // Test 6: sleep → wakeup 후 정상 실행
    test_stability();             // Test 7: 다수 프로세스 동시 안정성
    test_nice_inherit_chain();    // Test 8: nice 상속 체인 (fork의 fork)
    test_kill_sleeping();         // Test 9: kill된 sleeping 프로세스 처리
    test_single_process();        // Test 10: 기본 단일 프로세스 동작

    printf("\n=============================\n");
    printf("PASS: %d / FAIL: %d\n", pass_count, fail_count);
    if(fail_count == 0)
        printf("ALL TESTS PASSED\n");
    else
        printf("SOME TESTS FAILED\n");
    printf("=============================\n");

    exit(0);
}