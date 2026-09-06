/* Host-only behavior test with three controlled library imports. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
static int fake_pid, fake_write, exit_code;
static char observed[100];
static jmp_buf done;
int hello_getpid(void) { return fake_pid; }
int hello_write(int fd,const void *p,unsigned int n) { assert(fd==1); assert(n<sizeof(observed));memcpy(observed,p,n);observed[n]=0;return fake_write<0?fake_write:(int)n-fake_write; }
void hello_exit(int x) __attribute__((noreturn));
void hello_exit(int x) { exit_code=x;longjmp(done,1); }
#define getpid hello_getpid
#define write hello_write
#define _exit hello_exit
#define start hello_start
#include "hello.c"
#undef start
int main(void) {
 int checks=0;
 for (int i=0;i<5;i++) { int values[]={2,42,32767,1234567890,2147483647};fake_pid=values[i];fake_write=0;
  if (!setjmp(done)) hello_start();
  char expected[100];snprintf(expected,sizeof(expected),"IPHONE1_OWN_PROGRAM_V1 pid=%010d\n",fake_pid);
  assert(strcmp(observed,expected)==0);assert(exit_code==0);checks+=2;
 }
 for (int i=0;i<3;i++){fake_pid=42;fake_write=(int[]){1,5,-1}[i];if(!setjmp(done))hello_start();assert(exit_code==71);checks++;}
 fake_pid=1;fake_write=0;if(!setjmp(done))hello_start();assert(exit_code==71);checks++;
 printf("PASS %d host behavior checks; no guest execution\n",checks);
}
