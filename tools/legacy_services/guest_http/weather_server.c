/* Independently authored persistent OS1 local demo service, using ordinary libSystem.
 * Wire/layout constants: retained Apple historical XNU public BSD headers.
 * No SDK implementation, direct syscall, interface mutation, or host shortcut.
 */
typedef unsigned int u32;
typedef unsigned char u8;
struct address { u8 length, family, port[2], ip[4], zero[8]; };
struct pollitem { int fd; short events, revents; };
struct ifconfig { int length; void *buffer; };
#ifndef OWN_HOST_TEST
_Static_assert(sizeof(void *)==4 && sizeof(struct ifconfig)==8,"Darwin32 pointer ABI");
#endif
_Static_assert(sizeof(struct address)==16 && sizeof(struct pollitem)==8,"BSD layouts");
#define IMPORT(result,name,args) extern result name args; static result (*volatile api_##name) args = name
IMPORT(int,socket,(int,int,int)); IMPORT(int,bind,(int,const void *,u32));
IMPORT(int,listen,(int,int));
IMPORT(int,accept,(int,void *,u32 *)); IMPORT(int,getsockname,(int,void *,u32 *));
IMPORT(int,setsockopt,(int,int,int,const void *,u32));
IMPORT(int,fcntl,(int,int,...)); IMPORT(int,ioctl,(int,u32,...));
IMPORT(int,poll,(struct pollitem *,u32,int));
IMPORT(int,sysctl,(int *,u32,void *,u32 *,const void *,u32));
IMPORT(int,read,(int,void *,u32)); IMPORT(int,write,(int,const void *,u32));
IMPORT(int,recvfrom,(int,void *,u32,int,void *,u32 *));
IMPORT(int,sendto,(int,const void *,u32,int,const void *,u32));
IMPORT(int,close,(int)); IMPORT(int,usleep,(u32)); IMPORT(int *,__error,(void));
IMPORT(void,_exit,(int));
#include "weather_http.h"
#include "dns_packet.h"
#ifndef OWN_HTTP_DIAGNOSTICS
#define OWN_HTTP_DIAGNOSTICS 0
#endif
#ifndef OWN_HOST_TEST
#define OWN_SERVER_KEEP_RUNNING() 1
#endif
#if OWN_HTTP_DIAGNOSTICS
#define diagnostic_number number
#define diagnostic_line logline
#else
#define diagnostic_number(label, result, detail) ((void)0)
#define diagnostic_line(text) ((void)0)
#endif
static unsigned int log_failures;
static u32 length(const char *s){u32 n=0;while(s[n] && n<1024)n++;return n;}
static int equal(const void *a,const void *b,u32 n){const u8*x=a,*y=b;for(u32 i=0;i<n;i++)if(x[i]!=y[i])return 0;return 1;}
static int err(void){return *api___error();}
static void logline(const char*s){u32 n=length(s),off=0;for(int k=0;k<16 && off<n;k++){int r=api_write(1,s+off,n-off);if(r>0 && (u32)r<=n-off)off+=(u32)r;else if(r<0 && err()==4)continue;else break;}if(off!=n)log_failures++;}
static char hexchar(u32 n){return "0123456789abcdef"[n&15];}
static __attribute__((noinline)) void number(const char *label,int result,int error){char b[100];u32 n=length(label);if(n>70)return;for(u32 i=0;i<n;i++)b[i]=label[i];b[n++]=' ';for(int i=7;i>=0;i--)b[n++]=hexchar((u32)result>>(i*4));b[n++]=' ';for(int i=7;i>=0;i--)b[n++]=hexchar((u32)error>>(i*4));b[n++]='\n';b[n]=0;logline(b);}
#if OWN_HTTP_DIAGNOSTICS
static __attribute__((noinline)) void bytes(const char*label,const void*data,u32 count){const u8*p=data;number(label,(int)count,0);for(u32 off=0;off<count;){char b[100];u32 n=0;b[n++]='D';b[n++]=' ';for(u32 k=0;k<32 && off<count;k++,off++){b[n++]=hexchar(p[off]>>4);b[n++]=hexchar(p[off]);}b[n++]='\n';b[n]=0;logline(b);}}
#else
#define bytes(label, data, count) ((void)0)
#endif
static int waitfd(int fd,short events){for(int k=0;k<20;k++){struct pollitem p={fd,events,0};int r=api_poll(&p,1,100);if(r<0 && err()==4)continue;if(r<0){number("poll_error",r,err());return 0;}if(r>0){if(p.revents&0x20)return 0;return (p.revents&(events|0x18))!=0;}}number("poll_budget",fd,events);return 0;}
static int nonblock(int fd){int r=api_fcntl(fd,3);if(r<0)number("fcntl_get",r,err());if(r<0)return 0;r=api_fcntl(fd,4,r|4);if(r<0)number("fcntl_set",r,err());if(r<0)return 0;int one=1;r=api_setsockopt(fd,0xffff,0x1022,&one,4);if(r<0)number("nosigpipe",r,err());return r==0;}
static int send_all(int fd,const void*data,u32 n){const u8*p=data;u32 off=0;unsigned waits=0;for(int k=0;k<2048 && off<n;k++){int r=api_write(fd,p+off,n-off);if(r>0 && (u32)r<=n-off){off+=(u32)r;continue;}if(r<0 && err()==4)continue;if(r<0 && err()==35 && waits++<4 && waitfd(fd,4))continue;number("send_error",r,r<0?err():0);return 0;}return off==n;}
/* SIOCGIFCONF returns variable sockaddr records, max(name16 + sockaddr16,len).
 * Preserve all bytes and query flags separately; never set interface state.
 */
static int interfaces(int fd){u8 buffer[2048];struct ifconfig c={(int)sizeof(buffer),buffer};int r=api_ioctl(fd,0xc0086924U,&c);number("ifconf",r,r<0?err():c.length);if(r<0 || c.length<0 || (u32)c.length>=sizeof(buffer))return -1;bytes("ifconf_bytes",buffer,(u32)c.length);int found=0;u32 off=0;
 while(off<(u32)c.length){if((u32)c.length-off<32)return -1;u8*row=buffer+off;u32 size=16+(row[16]>16?row[16]:16);if(size>(u32)c.length-off)return -1;u8 flags[32]={0};for(u32 i=0;i<16;i++)flags[i]=row[i];r=api_ioctl(fd,0xc0206911U,flags);number("ifflags",r,r<0?err():(flags[16]|flags[17]<<8));bytes("ifname",row,16);if(r==0 && (flags[16]&1) && row[17]==2 && row[16]>=8 && row[20]==127 && row[21]==0 && row[22]==0 && row[23]==1)found=1;off+=size;}
 return found;
}
static int routes(void){u8 data[4096];u32 n=sizeof(data);int mib[6]={4,17,0,2,1,0};int r=api_sysctl(mib,6,data,&n,0,0);number("route_sysctl",r,r<0?err():(int)n);if(r<0 || n>sizeof(data))return 0;bytes("route_bytes",data,n);return 1;}
/* Independent port-80 Weather transport. Complete bounded POST, no proxy.
 * Ordinary socket ownership/readiness functions are copied from server.c;
 * that original Safari executable remains unchanged. */
static int receive_request(int fd,u8*data,u32 cap,u32*length_out){
 u32 used=0;unsigned waits=0;
 for(unsigned k=0;k<512 && used<cap;k++){
  int r=api_read(fd,data+used,cap-used);
  if(r>0&&(u32)r<=cap-used){used+=(u32)r;unsigned off=0,body=0;int state=own_weather_http_frame(data,used,&off,&body);if(state<0){number("WEATHER_FRAME_REJECTED",(int)used,0);bytes("WEATHER_REJECTED_REQUEST_BYTES",data,used);return 0;}if(state==1){*length_out=used;return 1;}continue;}
  if(r<0&&err()==4)continue;
  if(r<0&&err()==35&&waits++<4&&waitfd(fd,1))continue;
  number("weather_read_error",r,r<0?err():0);return 0;
 }
 number("weather_request_bound",(int)used,0);return 0;
}
static int serve_client(int fd,unsigned serial){
 u8 input[OWN_WEATHER_WIRE_CAP],output[OWN_WEATHER_RESPONSE_CAP];u32 n=0,out=0;
 diagnostic_number("WEATHER_REQUEST_BEGIN",(int)serial,fd);
 if(!receive_request(fd,input,sizeof input,&n))return 0;
 bytes("WEATHER_REQUEST_BYTES",input,n);
 if(!own_weather_http_response(input,n,output,sizeof output,&out)){logline("WEATHER_BUILD_FAILED\n");return 0;}
 bytes("WEATHER_RESPONSE_BYTES",output,out);
 if(!send_all(fd,output,out))return 0;
 diagnostic_number("WEATHER_RESPONSE_SENT",(int)serial,(int)out);return 1;
}
/* One bounded datagram per readiness turn. recv cap+1 detects oversize packets
 * even when the kernel truncates a larger UDP datagram. Replies never retry a
 * partial datagram or go to a non-loopback peer. */
static void serve_dns(int fd){
 u8 input[OWN_DNS_PACKET_CAP+1],output[OWN_DNS_PACKET_CAP];
 struct address peer={0};u32 size=sizeof peer,out=0;int r=-1;
 for(unsigned k=0;k<4;k++){size=sizeof peer;r=api_recvfrom(fd,input,sizeof input,0,&peer,&size);if(r<0&&err()==4)continue;break;}
 if(r<0){if(err()!=35&&err()!=4)number("DNS_RECV_ERROR",r,err());return;}
 bytes("DNS_PEER_BYTES",&peer,sizeof peer);
 if((u32)r<=sizeof input)bytes("DNS_QUERY_BYTES",input,(u32)r);
 if(size!=16||peer.length!=16||peer.family!=2||peer.ip[0]!=127||peer.ip[1]||peer.ip[2]||peer.ip[3]!=1||!(peer.port[0]|peer.port[1])||(u32)r>OWN_DNS_PACKET_CAP){number("DNS_PEER_OR_SIZE_REJECTED",r,(int)size);return;}
 if(!own_dns_response(input,(u32)r,output,sizeof output,&out)){number("DNS_PACKET_REJECTED",r,0);return;}
 bytes("DNS_RESPONSE_BYTES",output,out);
 for(unsigned k=0;k<4;k++){r=api_sendto(fd,output,out,0,&peer,sizeof peer);if(r<0&&err()==4)continue;break;}
 if(r!=(int)out)number("DNS_SEND_ERROR",r,r<0?err():0);
 else diagnostic_number("DNS_RESPONSE_SENT",r,0);
}
static int run(void){int server=-1,dns=-1,result=70;struct address a={16,2,{0,80},{127,0,0,1},{0}};logline("WEATHER_HTTP_BEGIN_V1\n");server=api_socket(2,1,0);number("socket_server",server,server<0?err():0);if(server<0)goto done;int ready=0;
 for(int k=0;k<30;k++){number("readiness_attempt",k,0);int state=interfaces(server);if(state<0)goto done;if(state==1){ready=1;break;}if(k==0)routes();if(k<29){int r=api_usleep(1000000);int e=r<0?err():0;number("readiness_sleep",r,e);if(r<0&&e!=4)goto done;}}
 if(!ready){logline("WEATHER_HTTP_NOT_READY_V1\n");goto done;}if(!routes()||!nonblock(server))goto done;int r=api_bind(server,&a,sizeof a);number("bind",r,r<0?err():0);if(r<0)goto done;struct address actual={0};u32 n=sizeof actual;r=api_getsockname(server,&actual,&n);number("bound_name",r,r<0?err():(int)n);bytes("bound_address",&actual,sizeof actual);if(r<0||n!=16||!equal(&actual,&a,8))goto done;r=api_listen(server,4);number("listen",r,r<0?err():0);if(r<0)goto done;logline("WEATHER_HTTP_LISTENING_V1\n");
 dns=api_socket(2,2,0);number("socket_dns",dns,dns<0?err():0);if(dns<0||!nonblock(dns))goto done;
 struct address da={16,2,{0,53},{127,0,0,1},{0}};r=api_bind(dns,&da,sizeof da);number("bind_dns",r,r<0?err():0);if(r<0)goto done;
 struct address dactual={0};n=sizeof dactual;r=api_getsockname(dns,&dactual,&n);if(r<0||n!=16||!equal(&dactual,&da,8))goto done;
 logline("DEMO_DNS_LISTENING_V1\n");
 unsigned served=0;while(OWN_SERVER_KEEP_RUNNING()){struct pollitem p[2]={{server,1,0},{dns,1,0}};r=api_poll(p,2,1000);if(r<0&&err()==4)continue;if(r<0){number("listener_poll_error",r,err());goto done;}if(r==0)continue;if((p[0].revents|p[1].revents)&0x38){number("listener_poll_events",p[0].revents,p[1].revents);goto done;}
 /* Both ready descriptors are serviced once. HTTP processing remains bounded
  * by its existing finite read/write/wait budgets; DNS never drains forever. */
 if(p[1].revents&1)serve_dns(dns);
 if(!(p[0].revents&1))continue;
 struct address peer={0};n=sizeof peer;int fd=api_accept(server,&peer,&n);int e=fd<0?err():0;if(fd<0&&e!=35&&e!=4)number("http_accept",fd,e);if(fd<0){if(e==35||e==4)continue;goto done;}served++;bytes("HTTP_PEER",&peer,sizeof peer);int ok=n==16&&peer.family==2&&equal(peer.ip,a.ip,4)&&nonblock(fd)&&serve_client(fd,served);r=api_close(fd);diagnostic_number("HTTP_CLOSED",(int)served,r<0?err():0);if(r<0)goto done;if(!ok)number("HTTP_REQUEST_REJECTED",(int)served,0);
 }
 /* Only the host contract seam can end the healthy service loop. */
 result=0;
 done:if(dns>=0){int r=api_close(dns);number("close_dns",r,r<0?err():0);if(r<0)result=70;}if(server>=0){int r=api_close(server);number("close_server",r,r<0?err():0);if(r<0)result=70;}if(log_failures)result=71;logline(result==0?"WEATHER_HTTP_END_V1\n":"WEATHER_HTTP_FAIL_V1\n");return result;
}
void start(void){api__exit(run());}
