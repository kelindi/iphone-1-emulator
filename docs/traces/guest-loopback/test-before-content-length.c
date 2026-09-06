#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define OWN_HOST_TEST 1
#define socket guest_socket
#define bind guest_bind
#define listen guest_listen
#define connect guest_connect
#define accept guest_accept
#define getsockname guest_getsockname
#define getsockopt guest_getsockopt
#define setsockopt guest_setsockopt
#define fcntl guest_fcntl
#define ioctl guest_ioctl
#define poll guest_poll
#define sysctl guest_sysctl
#define read guest_read
#define write guest_write
#define close guest_close
#define usleep guest_usleep
#define __error guest_error
#define _exit guest_exit
#include "loopback.c"
static int checks,errno_value,nextfd,ifcalls,sleeps,closes,mode,readstep,writestep;
static unsigned int rqsent,rpread,rpsent,rqread;
static char logs[20000];static unsigned int logsused;
#define CHECK(x) do {checks++;assert(x);}while(0)
static void reset(void){errno_value=0;nextfd=3;ifcalls=sleeps=closes=readstep=writestep=0;rqsent=rpread=rpsent=rqread=logsused=0;log_failures=0;mode=0;memset(logs,0,sizeof(logs));}
int guest_socket(int family,int type,int protocol){CHECK(family==2&&type==1&&protocol==0);if(mode==1){errno_value=45;return -1;}return nextfd++;}
int guest_bind(int fd,const void*p,u32 n){const struct address*a=p;CHECK(fd==3&&n==16&&a->length==16&&a->family==2&&a->port[0]==0x46&&a->port[1]==0xa1&&a->ip[0]==127&&a->ip[3]==1);if(mode==2){errno_value=49;return -1;}return 0;}
int guest_listen(int fd,int n){CHECK(fd==3&&n==1);return 0;}
int guest_connect(int fd,const void*p,u32 n){(void)p;CHECK(fd==4&&n==16);errno_value=36;return -1;}
int guest_accept(int fd,void*p,u32*n){CHECK(fd==3&&*n==16);struct address a={16,2,{0x99,0x88},{127,0,0,1},{0}};memcpy(p,&a,16);return 5;}
int guest_getsockname(int fd,void*p,u32*n){CHECK(fd==3&&*n==16);struct address a={16,2,{0x46,0xa1},{127,0,0,1},{0}};memcpy(p,&a,16);return 0;}
int guest_getsockopt(int fd,int l,int key,void*p,u32*n){CHECK(fd==4&&l==0xffff&&key==0x1007&&*n==4);*(int*)p=mode==3?61:0;return 0;}
int guest_setsockopt(int fd,int l,int key,const void*p,u32 n){CHECK(fd>=3&&l==0xffff&&key==0x1022&&n==4&&*(const int*)p==1);return 0;}
int guest_fcntl(int fd,int cmd,...){CHECK(fd>=3&&(cmd==3||cmd==4));return 0;}
int guest_ioctl(int fd,u32 cmd,...){CHECK(fd==3);va_list a;va_start(a,cmd);void*p=va_arg(a,void*);va_end(a);if(cmd==0xc0086924U){struct ifconfig*c=p;CHECK(c->length==2048);ifcalls++;if(mode==4 || (mode==5&&ifcalls<3)){c->length=0;return 0;}u8 row[32]={0};row[0]='l';row[1]='o';row[2]='0';row[16]=16;row[17]=2;row[20]=127;row[23]=1;if(mode==6)row[16]=255;memcpy(c->buffer,row,32);c->length=32;return 0;}CHECK(cmd==0xc0206911U);((u8*)p)[16]=1;return 0;}
int guest_sysctl(int*m,u32 count,void*p,u32*n,const void*newp,u32 newn){CHECK(count==6&&m[0]==4&&m[1]==17&&m[2]==0&&m[3]==2&&m[4]==1&&m[5]==0&&*n==4096&&!newp&&!newn);if(mode==7){errno_value=12;return -1;}memset(p,0,16);*n=16;return 0;}
int guest_poll(struct pollitem*p,u32 n,int timeout){CHECK(n==1&&timeout==100);if(mode==8)return 0;p->revents=p->events;return 1;}
int guest_read(int fd,void*p,u32 n){readstep++;if(mode==9){errno_value=35;return -1;}if(readstep==1){errno_value=4;return -1;}if(readstep==2){errno_value=35;return -1;}const char*src=fd==5?request:response;u32 total=fd==5?sizeof(request)-1:sizeof(response)-1;u32*off=fd==5?&rqread:&rpread;CHECK(fd==5||fd==4);if(*off==total){if(mode==10){*(u8*)p=33;return 1;}return 0;}u32 take=total-*off;if(take>n)take=n;if(take>7)take=7;memcpy(p,src+*off,take);if(mode==11)((u8*)p)[0]^=1;*off+=take;return (int)take;}
int guest_write(int fd,const void*p,u32 n){if(fd==1){CHECK(logsused+n<sizeof(logs));memcpy(logs+logsused,p,n);logsused+=n;return (int)n;}writestep++;if(writestep==1){errno_value=4;return -1;}if(writestep==2){errno_value=35;return -1;}if(mode==12){errno_value=32;return -1;}const char*src=fd==4?request:response;u32 total=fd==4?sizeof(request)-1:sizeof(response)-1;u32*off=fd==4?&rqsent:&rpsent;CHECK(fd==4||fd==5);u32 take=n>7?7:n;CHECK(*off+take<=total&&memcmp(src+*off,p,take)==0);*off+=take;return (int)take;}
int guest_close(int fd){CHECK(fd>=3&&fd<=5);closes++;return 0;}
int guest_usleep(u32 n){CHECK(n==1000000);sleeps++;return 0;}
int*guest_error(void){return &errno_value;}
void guest_exit(int status){(void)status;CHECK(0);}
int main(void){
 CHECK(sizeof(request)-1<sizeof(response));CHECK(strlen("IPHONE1_GUEST_LOOPBACK_V1_OK\n")==27);
 reset();CHECK(run()==0);CHECK(closes==3&&sleeps==0&&strstr(logs,"LOOPBACK_PASS_V1"));CHECK(rqsent==sizeof(request)-1&&rqread==rqsent&&rpsent==sizeof(response)-1&&rpread==rpsent);
 for(int m=1;m<=12;m++){reset();mode=m;int r=run();if(m==5){CHECK(r==0&&sleeps==2&&closes==3);}else{CHECK(r!=0&&strstr(logs,"LOOPBACK_PASS_V1")==0);if(m==4)CHECK(sleeps==29&&ifcalls==30);}}
 printf("PASS %d assertions; 13 mocked workflows including partial/EINTR/EAGAIN, early readiness, errors, truncation, byte corruption, extra data and finite budgets. Not guest networking proof.\n",checks);
}
