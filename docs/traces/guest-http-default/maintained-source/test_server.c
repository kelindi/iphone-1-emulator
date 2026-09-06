#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define OWN_HOST_TEST 1
static int test_running(void);
#define OWN_SERVER_KEEP_RUNNING() test_running()
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
#include "server.c"
static unsigned checks,mode,accepted,closed,listener_polls,read_calls,write_calls,read_off,response_used,request_closed;
static unsigned long long wire_hash=1469598103934665603ull;
#define REQUESTS 256u
#define POLLS 4200u
static int error_value;static char captured[8192];static unsigned logs_used;static char logs[100000];
static const char*requests[]={"GET / HTTP/1.1\r\nHost: 127.0.0.1:18081\r\n\r\n","GET /next HTTP/1.0\r\n\r\n","GET /echo?message=hello HTTP/1.0\r\n\r\n"};
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"server:%d %s\n",__LINE__,#x);exit(1);}}while(0)
static void reset(unsigned m){mode=m;accepted=closed=listener_polls=read_calls=write_calls=read_off=response_used=request_closed=logs_used=log_failures=0;error_value=0;memset(captured,0,sizeof captured);memset(logs,0,sizeof logs);}
static int test_running(void){return listener_polls<POLLS;}
int guest_socket(int f,int t,int p){CHECK(f==2&&t==1&&p==0);return 3;}
int guest_bind(int fd,const void*p,u32 n){const struct address*a=p;CHECK(fd==3&&n==16&&a->family==2&&a->port[0]==0x46&&a->port[1]==0xa1&&a->ip[0]==127&&a->ip[3]==1);return 0;}
int guest_listen(int fd,int n){CHECK(fd==3&&n==4);return 0;}
int guest_connect(int fd,const void*p,u32 n){(void)fd;(void)p;(void)n;CHECK(0);return -1;}
int guest_accept(int fd,void*p,u32*n){CHECK(fd==3&&*n==16&&accepted<REQUESTS);struct address a={16,2,{0xc0,0},{127,0,0,1},{0}};if(mode==4)a.ip[0]=10;memcpy(p,&a,16);accepted++;read_calls=write_calls=read_off=response_used=0;memset(captured,0,sizeof captured);return 4;}
int guest_getsockname(int fd,void*p,u32*n){CHECK(fd==3&&*n==16);struct address a={16,2,{0x46,0xa1},{127,0,0,1},{0}};memcpy(p,&a,16);return 0;}
int guest_getsockopt(int fd,int l,int k,void*p,u32*n){(void)fd;(void)l;(void)k;(void)p;(void)n;CHECK(0);return -1;}
int guest_setsockopt(int fd,int l,int k,const void*p,u32 n){CHECK((fd==3||fd==4)&&l==0xffff&&k==0x1022&&n==4&&*(const int*)p==1);return 0;}
int guest_fcntl(int fd,int cmd,...){CHECK((fd==3||fd==4)&&(cmd==3||cmd==4));return 0;}
int guest_ioctl(int fd,u32 cmd,...){CHECK(fd==3);va_list a;va_start(a,cmd);void*p=va_arg(a,void*);va_end(a);if(cmd==0xc0086924U){struct ifconfig*c=p;CHECK(c->length==2048);u8 row[32]={0};row[0]='l';row[1]='o';row[2]='0';row[16]=16;row[17]=2;row[20]=127;row[23]=1;memcpy(c->buffer,row,32);c->length=32;return 0;}CHECK(cmd==0xc0206911U);((u8*)p)[16]=1;return 0;}
int guest_sysctl(int*m,u32 count,void*p,u32*n,const void*newp,u32 newn){CHECK(count==6&&m[0]==4&&m[1]==17&&*n==4096&&!newp&&!newn);memset(p,0,16);*n=16;return 0;}
int guest_poll(struct pollitem*p,u32 n,int timeout){CHECK(n==1);if(p->fd==3){CHECK(timeout==1000);listener_polls++;if(mode==3){error_value=9;return -1;}if(mode==6&&listener_polls<=3){error_value=4;return -1;}if(accepted<REQUESTS){p->revents=1;return 1;}return 0;}CHECK(p->fd==4&&timeout==100);if(mode==5)return 0;p->revents=p->events;return 1;}
int guest_read(int fd,void*p,u32 n){CHECK(fd==4&&accepted>=1&&accepted<=REQUESTS);read_calls++;if(read_calls==1){error_value=4;return -1;}if(read_calls==2||mode==5){error_value=35;return -1;}if(mode==1)return 0;const char*s=requests[(accepted-1)%3];unsigned left=(unsigned)strlen(s)-read_off;if(!left)return 0;unsigned take=left<n?left:n;if(take>7)take=7;memcpy(p,s+read_off,take);read_off+=take;return (int)take;}
int guest_write(int fd,const void*p,u32 n){if(fd==1){unsigned room=logs_used<sizeof logs-1?(unsigned)sizeof logs-1-logs_used:0;unsigned take=n<room?n:room;if(take)memcpy(logs+logs_used,p,take);logs_used+=n;return (int)n;}CHECK(fd==4);write_calls++;if(write_calls==1){error_value=4;return -1;}if(write_calls==2){error_value=35;return -1;}if(mode==2){error_value=32;return -1;}unsigned take=n>31?31:n;CHECK(response_used+take<sizeof captured);memcpy(captured+response_used,p,take);response_used+=take;for(unsigned i=0;i<take;i++)wire_hash=(wire_hash^((const unsigned char*)p)[i])*1099511628211ull;return (int)take;}
int guest_close(int fd){CHECK(fd==3||fd==4);closed++;if(fd==4){request_closed++;if(mode==0||mode==6){unsigned index=(accepted-1)%3;CHECK(strstr(captured,"HTTP/1.0 200 OK\r\n"));CHECK(strstr(captured,index==0?"HOME-1":index==1?"NEXT-1":"Message: hello"));CHECK(strstr(captured,index==0?"Local demo":"Local demo: fixed content"));CHECK(read_off==strlen(requests[index]));unsigned count=0;CHECK(sscanf(strstr(captured,"Content-Length:"),"Content-Length: %u",&count)==1);char*body=strstr(captured,"\r\n\r\n");CHECK(body&&count==response_used-(unsigned)(body+4-captured));}if(mode==1||mode==4||mode==5)CHECK(response_used==0);}return 0;}
int guest_usleep(u32 n){(void)n;CHECK(0);return -1;}
int*guest_error(void){return &error_value;}
void guest_exit(int result){(void)result;CHECK(0);}
int main(void){for(unsigned m=0;m<=6;m++){reset(m);int r=run();if(m==3){CHECK(r!=0&&listener_polls==1&&closed==1);}else{CHECK(r==0&&accepted==REQUESTS&&request_closed==REQUESTS&&closed==REQUESTS+1&&listener_polls==POLLS);if(m==0||m==6){CHECK(strstr(logs,"LOCAL_HTTP_LISTENING_V1"));
#if OWN_HTTP_DIAGNOSTICS
 CHECK(strstr(logs,"HTTP_RESPONSE_SENT")&&logs_used>100000);
#else
 CHECK(!strstr(logs,"HTTP_REQUEST_BYTES")&&!strstr(logs,"HTTP_RESPONSE_BYTES")&&!strstr(logs,"HTTP_RESPONSE_SENT")&&logs_used<2048);
#endif
 }else CHECK(strstr(logs,"HTTP_REQUEST_REJECTED"));}}
 printf("PASS %u persistent server ownership/partial-I/O/EOF/error/peer/timeout assertions; host mocks only\nWIRE %016llx\n",checks,wire_hash);
}
