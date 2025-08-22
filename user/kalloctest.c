#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NCHILD 2
#define N 100000
#define SZ 4096

void test1(void);
void test2(void);
void test3(void);
char buf[SZ];

int countfree();

int
main(int argc, char *argv[])
{
  test1();
  test2();
  test3();
  exit(0);
}

int ntas(int print)
{
  int n;
  char *c;

  if (statistics(buf, SZ) <= 0) {
    fprintf(2, "ntas: no stats\n");
  }
  c = strchr(buf, '=');
  n = atoi(c+2);
  if(print)
    printf("%s", buf);
  return n;
}

// Test concurrent kallocs and kfrees
void test1(void)
{
  void *a, *a1;
  int n, m;

  printf("start test1\n");
  m = ntas(0); // 开始时"="之后的值
  for(int i = 0; i < NCHILD; i++){ // 两个子进程
    int pid = fork();
    if(pid < 0){
      printf("fork failed");
      exit(-1);
    }
    if(pid == 0){
      for(i = 0; i < N; i++) { // 100000次增减内存空间
        a = sbrk(4096);
        *(int *)(a+4) = 1;
        a1 = sbrk(-4096); // 只是在不停增减1页
        if (a1 != a + 4096) {
          printf("test1: FAIL wrong sbrk\n");
          exit(1);
        }
      }
      exit(0);
    }
  }
  int status = 0;
  for(int i = 0; i < NCHILD; i++){
    wait(&status);
    if (status != 0) {
      printf("FAIL: a child failed\n");
      exit(1);
    }
  }
  printf("test1 results:\n");
  n = ntas(1); // 结束时"="之后的的值
  if(n-m < 10)
    printf("test1 OK\n");
  else
    printf("test1 FAIL\n");
}


// Test stealing
void test2() {
  int free0 = countfree(); // 进程能分配的最大页数
  int free1;
  int n = (PHYSTOP-KERNBASE)/PGSIZE;
  printf("start test2\n");
  printf("total free number of pages: %d (out of %d)\n", free0, n);
  if(n - free0 > 1000) {
    printf("test2 FAILED: cannot allocate enough memory");
    exit(1);
  }
  for (int i = 0; i < 50; i++) {
    free1 = countfree();
    if(i % 10 == 9) // 每10次打印一次.
      printf(".");
    if(free1 != free0) { // 反复调用countfree 看是否有内存泄漏
      printf("test2 FAIL: losing pages %d %d\n", free0, free1);
      exit(1);
    }
  }
  printf("\ntest2 OK\n");
}

// Test concurrent kalloc/kfree and stealing
void test3(void)
{
  uint64 a, a1;
  int n, m;

  m = ntas(0); // 开始前"="之后的值
  printf("start test3\n");
  int pid;

  for(int i = 0; i < NCHILD; i++){ // 2个子进程
    pid = fork();
    if(pid < 0){
      printf("fork failed");
      exit(-1);
    }
    if(pid == 0){
      if (i == 0) { // 第1个子进程
        for(i = 0; i < N; i++) { // 不断申请内存
          a = (uint64) sbrk(4096);
          if(a == 0xffffffffffffffff){
            // no freemem
            continue;
          }
          *(int *)(a+4) = 1;
          a1 = (uint64) sbrk(-4096);
          if (a1 != a + 4096) {
            printf("test3 FAIL: wrong sbrk\n");
            exit(1);
          }
          if ((i + 1) % 10000 == 0) {
            printf(".");
          }
        }
        printf("child done %d\n", i);
        exit(0);
      } else { // 第2个子进程
        while (1) { // 死循环执行
          int free0 = countfree();
          int free1 = countfree();
          if(free0 - free1 > 1) {
            printf("test3 FAIL: losing pages %d %d\n", free0, free1);
            exit(1);
          }
        }
      }
    }
  }

  int status = 0;
  for(int i = 0; i < NCHILD-1; i++){ // 只回收第1个进程的资源
    wait(&status);
    if (status != 0) {
      printf("a child failed\n");
      exit(1);
    }
  }
  kill(pid); // 结束所有子进程

  n = ntas(1); // 结束时"="之后的值
  if(n-m < 10000)
    printf("\ntest3 OK\n");
  else
    printf("test3 FAIL m %d n %d\n", m, n);
}


//
// countfree() from usertests.c
//
int
countfree()
{
  uint64 sz0 = (uint64)sbrk(0);
  int n = 0;

  while(1){
    uint64 a = (uint64) sbrk(4096);
    if(a == 0xffffffffffffffff){
      break;
    }
    // modify the memory to make sure it's really allocated.
    *(char *)(a + 4096 - 1) = 1;
    n += 1;
  }
  sbrk(-((uint64)sbrk(0) - sz0));
  return n;
}
