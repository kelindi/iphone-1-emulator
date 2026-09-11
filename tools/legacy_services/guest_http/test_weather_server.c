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
#define recvfrom guest_recvfrom
#define sendto guest_sendto
#define read guest_read
#define write guest_write
#define close guest_close
#define usleep guest_usleep
#define __error guest_error
#define _exit guest_exit
#include "weather_server.c"
static unsigned checks,mode,accepted,closed,listener_polls,read_calls,write_calls,read_off,response_used,request_closed;
static unsigned dns_received,dns_sent,dns_last_turn,dns_closed;
static const unsigned char dns_query[]={0x12,0x34,1,0,0,1,0,0,0,0,0,0,9,'i','p','h','o','n','e','-','w','u',5,'a','p','p','l','e',3,'c','o','m',0,0,1,0,1};
static unsigned long long wire_hash=1469598103934665603ull;
#define REQUESTS 14u
#define POLLS 40u
static int error_value;static char captured[8192];static unsigned logs_used;static char logs[100000];
static const char*requests[]={"POST /dgw?imei=demo&apptype=weather&t=3 HTTP/1.1\r\nHost: iphone-wu.apple.com\r\nContent-Type: text/xml\r\nContent-Length: 379\r\n\r\n<?xml version=\"1.0\" encoding=\"utf-8\"?><request devtype=\"Apple iPhone v1.0\" deployver=\"Apple iPhone v1.0\" app=\"YGoiPhoneClient\" appver=\"1.0.0.demo\" api=\"weather\" apiver=\"1.0.0\" acknotification=\"0000\"><query id=\"30\" timestamp=\"0\" type=\"getforecastbylocationid\"><list><id>USCA0273|12797509</id><id>USNY0996|2459115</id></list><language>en</language><unit>f</unit></query></request>\n","POST /dgw?imei=demo&apptype=weather&t=3 HTTP/1.1\r\nHost: iphone-wu.apple.com\r\nContent-Type: text/xml\r\nContent-Length: 293\r\n\r\n<?xml version=\"1.0\" encoding=\"utf-8\"?><request devtype=\"Apple iPhone v1.0\" deployver=\"Apple iPhone v1.0\" app=\"YGoiPhoneClient\" appver=\"1.0.0.demo\" api=\"weather\" apiver=\"1.0.0\" acknotification=\"0000\"><query id=\"3\" timestamp=\"0\" type=\"getlocationid\"><phrase>Cupertino</phrase></query></request>\n","POST /dgw?imei=demo&apptype=weather&t=3 HTTP/1.1\r\nHost: iphone-wu.apple.com\r\nContent-Type: text/xml\r\nContent-Length: 293\r\n\r\n<?xml version=\"1.0\" encoding=\"utf-8\"?><request devtype=\"Apple iPhone v1.0\" deployver=\"Apple iPhone v1.0\" app=\"YGoiPhoneClient\" appver=\"1.0.0.demo\" api=\"weather\" apiver=\"1.0.0\" acknotification=\"0000\"><query id=\"3\" timestamp=\"0\" type=\"getlocationid\"><phrase>Cupertino</phrase></query></request>\n","GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n","GET /next HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n","GET /echo?message=hi HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n","POST /dgw?imei=demo&apptype=finance HTTP/1.1\r\nHost: iphone-wu.apple.com\r\nContent-Type: text/xml\r\nContent-Length: 123\r\n\r\n<request api=\"finance\" apiver=\"1.0.0\"><query id=\"123\" type=\"getquotes\"><list><symbol>AAPL</symbol></list></query></request>"};
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"server:%d %s\n",__LINE__,#x);exit(1);}}while(0)
static void reset(unsigned m){mode=m;accepted=closed=listener_polls=read_calls=write_calls=read_off=response_used=request_closed=logs_used=log_failures=0;dns_received=dns_sent=dns_last_turn=dns_closed=0;error_value=0;memset(captured,0,sizeof captured);memset(logs,0,sizeof logs);}
static int test_running(void){return listener_polls<POLLS;}
int guest_socket(int f,int t,int p){CHECK(f==2&&(t==1||t==2)&&p==0);if(t==2&&mode==16){error_value=24;return -1;}return t==1?3:5;}
int guest_bind(int fd,const void*p,u32 n){const struct address*a=p;CHECK((fd==3||fd==5)&&n==16&&a->length==16&&a->family==2&&a->port[0]==0&&a->port[1]==(fd==3?80:53)&&a->ip[0]==127&&a->ip[1]==0&&a->ip[2]==0&&a->ip[3]==1);if(fd==5&&mode==13){error_value=48;return -1;}return 0;}
int guest_listen(int fd,int n){CHECK(fd==3&&n==4);return 0;}
int guest_connect(int fd,const void*p,u32 n){(void)fd;(void)p;(void)n;CHECK(0);return -1;}
int guest_accept(int fd,void*p,u32*n){CHECK(fd==3&&*n==16&&accepted<REQUESTS&&dns_last_turn==listener_polls);struct address a={16,2,{0xc0,0},{127,0,0,1},{0}};if(mode==4)a.ip[0]=10;memcpy(p,&a,16);accepted++;read_calls=write_calls=read_off=response_used=0;memset(captured,0,sizeof captured);return 4;}
int guest_getsockname(int fd,void*p,u32*n){CHECK((fd==3||fd==5)&&*n==16);struct address a={16,2,{0,80},{127,0,0,1},{0}};if(fd==5)a.port[1]=mode==18?54:53;memcpy(p,&a,16);return 0;}
int guest_getsockopt(int fd,int l,int k,void*p,u32*n){(void)fd;(void)l;(void)k;(void)p;(void)n;CHECK(0);return -1;}
int guest_setsockopt(int fd,int l,int k,const void*p,u32 n){CHECK((fd==3||fd==4||fd==5)&&l==0xffff&&k==0x1022&&n==4&&*(const int*)p==1);return 0;}
int guest_fcntl(int fd,int cmd,...){CHECK((fd==3||fd==4||fd==5)&&(cmd==3||cmd==4));if(fd==5&&mode==17){error_value=9;return -1;}return 0;}
int guest_ioctl(int fd,u32 cmd,...){CHECK(fd==3);va_list a;va_start(a,cmd);void*p=va_arg(a,void*);va_end(a);if(cmd==0xc0086924U){struct ifconfig*c=p;CHECK(c->length==2048);u8 row[32]={0};row[0]='l';row[1]='o';row[2]='0';row[16]=16;row[17]=2;row[20]=127;row[23]=1;memcpy(c->buffer,row,32);c->length=32;return 0;}CHECK(cmd==0xc0206911U);((u8*)p)[16]=1;return 0;}
int guest_sysctl(int*m,u32 count,void*p,u32*n,const void*newp,u32 newn){CHECK(count==6&&m[0]==4&&m[1]==17&&*n==4096&&!newp&&!newn);memset(p,0,16);*n=16;return 0;}
int guest_poll(struct pollitem*p,u32 n,int timeout){if(n==2){CHECK(p[0].fd==3&&p[1].fd==5&&timeout==1000&&p[0].events==1&&p[1].events==1);listener_polls++;if(mode==3){error_value=9;return -1;}if(mode==6&&listener_polls<=3){error_value=4;return -1;}p[1].revents=1;if(mode<8&&accepted<REQUESTS){p[0].revents=1;return 2;}return 1;}CHECK(n==1&&p->fd==4&&timeout==100);if(mode==5)return 0;p->revents=p->events;return 1;}
int guest_recvfrom(int fd,void*p,u32 cap,int flags,void*address,u32*n){CHECK(fd==5&&cap==OWN_DNS_PACKET_CAP+1&&flags==0&&*n==16);dns_received++;dns_last_turn=listener_polls;if(mode==12){error_value=4;return -1;}if(mode==14){error_value=35;return -1;}struct address a={16,2,{0x80,1},{127,0,0,1},{0}};if(mode==8)a.ip[0]=10;memcpy(address,&a,16);if(mode==10){memset(p,0,cap);return (int)cap;}memcpy(p,dns_query,sizeof dns_query);if(mode==9)((u8*)p)[4]=1;return sizeof dns_query;}
int guest_sendto(int fd,const void*p,u32 n,int flags,const void*address,u32 size){const struct address*a=address;const u8*b=p;CHECK(fd==5&&flags==0&&size==16&&a->ip[0]==127&&a->ip[1]==0&&a->ip[2]==0&&a->ip[3]==1&&a->port[0]==0x80&&a->port[1]==1);CHECK(n==sizeof dns_query+16&&b[0]==0x12&&b[1]==0x34&&b[2]==0x85&&b[3]==0&&b[7]==1);CHECK(memcmp(b+12,dns_query+12,sizeof dns_query-12)==0&&memcmp(b+n-4,"\177\0\0\1",4)==0);dns_sent++;if(mode==11){error_value=35;return -1;}if(mode==15)return (int)n-1;return (int)n;}

int guest_read(int fd,void*p,u32 n){CHECK(fd==4&&accepted>=1&&accepted<=REQUESTS);read_calls++;if(read_calls==1){error_value=4;return -1;}if(read_calls==2||mode==5){error_value=35;return -1;}if(mode==1)return 0;const char*s=requests[(accepted-1)%7];unsigned left=(unsigned)strlen(s)-read_off;if(mode==7){unsigned bound=(unsigned)strlen(s)-10;if(read_off>=bound)return 0;left=bound-read_off;}if(!left)return 0;unsigned take=left<n?left:n;if(take>7)take=7;memcpy(p,s+read_off,take);read_off+=take;return (int)take;}
int guest_write(int fd,const void*p,u32 n){if(fd==1){unsigned room=logs_used<sizeof logs-1?(unsigned)sizeof logs-1-logs_used:0;unsigned take=n<room?n:room;if(take)memcpy(logs+logs_used,p,take);logs_used+=n;return (int)n;}CHECK(fd==4);write_calls++;if(write_calls==1){error_value=4;return -1;}if(write_calls==2){error_value=35;return -1;}if(mode==2){error_value=32;return -1;}unsigned take=n>31?31:n;CHECK(response_used+take<sizeof captured);memcpy(captured+response_used,p,take);response_used+=take;for(unsigned i=0;i<take;i++)wire_hash=(wire_hash^((const unsigned char*)p)[i])*1099511628211ull;return (int)take;}
int guest_close(int fd){CHECK(fd==3||fd==4||fd==5);closed++;if(fd==5)dns_closed++;if(fd==4){request_closed++;if(mode==0||mode==6){unsigned index=(accepted-1)%7;CHECK(strstr(captured,"HTTP/1.0 200 OK\r\n"));if(index<3){CHECK(strstr(captured,"Cupertino"));CHECK(strstr(captured,"X-iPhone1-Demo: fixed-weather"));}else if(index==6){CHECK(strstr(captured,"<symbol>AAPL</symbol>"));CHECK(strstr(captured,"X-iPhone1-Demo: fixed-stocks"));}else{CHECK(strstr(captured,index==3?"HOME-1":index==4?"NEXT-1":"Message: hi"));CHECK(strstr(captured,"text/html"));}CHECK(read_off==strlen(requests[index]));unsigned count=0;CHECK(sscanf(strstr(captured,"Content-Length:"),"Content-Length: %u",&count)==1);char*body=strstr(captured,"\r\n\r\n");CHECK(body&&count==response_used-(unsigned)(body+4-captured));}if(mode==1||mode==4||mode==5||mode==7)CHECK(response_used==0);}return 0;}
int guest_usleep(u32 n){(void)n;CHECK(0);return -1;}
int*guest_error(void){return &error_value;}
void guest_exit(int result){(void)result;CHECK(0);}
int main(void){for(unsigned m=0;m<=18;m++){reset(m);int r=run();if(m==3){CHECK(r!=0&&listener_polls==1&&closed==2&&dns_closed==1);}else if(mode==16){CHECK(r!=0&&closed==1&&dns_closed==0&&listener_polls==0);}else if(mode==13||mode==17||mode==18){CHECK(r!=0&&closed==2&&dns_closed==1&&listener_polls==0);}else if(mode>=8){CHECK(r==0&&accepted==0&&closed==2&&dns_closed==1&&listener_polls==POLLS);CHECK(dns_received==(mode==12?POLLS*4:POLLS));CHECK(dns_sent==((mode==11||mode==15)?POLLS:0));}else{CHECK(r==0&&accepted==REQUESTS&&request_closed==REQUESTS&&closed==REQUESTS+2&&dns_closed==1&&listener_polls==POLLS);if(m==0||m==6){CHECK(strstr(logs,"WEATHER_HTTP_LISTENING_V1"));
#if OWN_HTTP_DIAGNOSTICS
 CHECK(strstr(logs,"WEATHER_RESPONSE_SENT")&&logs_used>5000);
#else
 CHECK(!strstr(logs,"WEATHER_REQUEST_BYTES")&&!strstr(logs,"WEATHER_RESPONSE_BYTES")&&!strstr(logs,"WEATHER_RESPONSE_SENT")&&logs_used<8192);
#endif
 }else CHECK(strstr(logs,"HTTP_REQUEST_REJECTED"));}}
 const char*saved=requests[0];requests[0]="POST /dgw?apptype=weather&t=4294967296 HTTP/1.1\r\nHost: iphone-wu.apple.com\r\nContent-Type: text/xml\r\nContent-Length: 1\r\n\r\nx";reset(0);accepted=1;unsigned char rejected[OWN_WEATHER_WIRE_CAP];unsigned used=0;CHECK(!receive_request(4,rejected,sizeof rejected,&used));CHECK(strstr(logs,"WEATHER_FRAME_REJECTED"));
#if OWN_HTTP_DIAGNOSTICS
 CHECK(strstr(logs,"WEATHER_REJECTED_REQUEST_BYTES")&&strstr(logs,"D 504f5354202f6467773f"));
#else
 CHECK(!strstr(logs,"WEATHER_REJECTED_REQUEST_BYTES"));
#endif
 requests[0]=saved;
 printf("PASS %u HTTP80 plus UDP53/body/ownership/partial-I/O/EOF/error/peer/timeout/fairness assertions; host mocks only\nWIRE %016llx\n",checks,wire_hash);
}
