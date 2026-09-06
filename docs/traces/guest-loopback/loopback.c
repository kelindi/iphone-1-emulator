/* Independently authored OS1 loopback qualification, using ordinary libSystem.
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
IMPORT(int,listen,(int,int)); IMPORT(int,connect,(int,const void *,u32));
IMPORT(int,accept,(int,void *,u32 *)); IMPORT(int,getsockname,(int,void *,u32 *));
IMPORT(int,getsockopt,(int,int,int,void *,u32 *)); IMPORT(int,setsockopt,(int,int,int,const void *,u32));
IMPORT(int,fcntl,(int,int,...)); IMPORT(int,ioctl,(int,u32,...));
IMPORT(int,poll,(struct pollitem *,u32,int));
IMPORT(int,sysctl,(int *,u32,void *,u32 *,const void *,u32));
IMPORT(int,read,(int,void *,u32)); IMPORT(int,write,(int,const void *,u32));
IMPORT(int,close,(int)); IMPORT(int,usleep,(u32)); IMPORT(int *,__error,(void));
IMPORT(void,_exit,(int));
static const char request[]="GET /qualification HTTP/1.0\r\nHost: 127.0.0.1:18081\r\n\r\n";
static const char response[]="HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 29\r\nConnection: close\r\n\r\nIPHONE1_GUEST_LOOPBACK_V1_OK\n";
static unsigned int log_failures;
static u32 length(const char *s){u32 n=0;while(s[n] && n<1024)n++;return n;}
static int equal(const void *a,const void *b,u32 n){const u8*x=a,*y=b;for(u32 i=0;i<n;i++)if(x[i]!=y[i])return 0;return 1;}
static int err(void){return *api___error();}
static void logline(const char*s){u32 n=length(s),off=0;for(int k=0;k<16 && off<n;k++){int r=api_write(1,s+off,n-off);if(r>0 && (u32)r<=n-off)off+=(u32)r;else if(r<0 && err()==4)continue;else break;}if(off!=n)log_failures++;}
static char hexchar(u32 n){return "0123456789abcdef"[n&15];}
static __attribute__((noinline)) void number(const char *label,int result,int error){char b[100];u32 n=length(label);if(n>70)return;for(u32 i=0;i<n;i++)b[i]=label[i];b[n++]=' ';for(int i=7;i>=0;i--)b[n++]=hexchar((u32)result>>(i*4));b[n++]=' ';for(int i=7;i>=0;i--)b[n++]=hexchar((u32)error>>(i*4));b[n++]='\n';b[n]=0;logline(b);}
static __attribute__((noinline)) void bytes(const char*label,const void*data,u32 count){const u8*p=data;number(label,(int)count,0);for(u32 off=0;off<count;){char b[100];u32 n=0;b[n++]='D';b[n++]=' ';for(u32 k=0;k<32 && off<count;k++,off++){b[n++]=hexchar(p[off]>>4);b[n++]=hexchar(p[off]);}b[n++]='\n';b[n]=0;logline(b);}}
static int waitfd(int fd,short events){for(int k=0;k<20;k++){struct pollitem p={fd,events,0};int r=api_poll(&p,1,100);if(r<0 && err()==4)continue;if(r<0){number("poll_error",r,err());return 0;}if(r>0){if(p.revents&0x20)return 0;return (p.revents&(events|0x18))!=0;}}number("poll_budget",fd,events);return 0;}
static int nonblock(int fd){int r=api_fcntl(fd,3);number("fcntl_get",r,r<0?err():0);if(r<0)return 0;r=api_fcntl(fd,4,r|4);number("fcntl_set",r,r<0?err():0);if(r<0)return 0;int one=1;r=api_setsockopt(fd,0xffff,0x1022,&one,4);number("nosigpipe",r,r<0?err():0);return r==0;}
static int send_all(int fd,const void*data,u32 n){const u8*p=data;u32 off=0;for(int k=0;k<64 && off<n;k++){int r=api_write(fd,p+off,n-off);if(r>0 && (u32)r<=n-off){off+=(u32)r;continue;}if(r<0 && err()==4)continue;if(r<0 && err()==35 && waitfd(fd,4))continue;number("send_error",r,r<0?err():0);return 0;}return off==n;}
static int recv_exact(int fd,void*data,u32 n,int eof){u8*p=data;u32 off=0;for(int k=0;k<64;k++){u8 extra;int r=api_read(fd,off<n?p+off:&extra,off<n?n-off:1);if(r>0){if((u32)r>n-off)return 0;off+=(u32)r;if(off==n && !eof)return 1;continue;}if(r==0)return eof && off==n;if(err()==4)continue;if(err()==35 && waitfd(fd,1))continue;number("recv_error",r,err());return 0;}return 0;}
/* SIOCGIFCONF returns variable sockaddr records, max(name16 + sockaddr16,len).
 * Preserve all bytes and query flags separately; never set interface state.
 */
static int interfaces(int fd){u8 buffer[2048];struct ifconfig c={(int)sizeof(buffer),buffer};int r=api_ioctl(fd,0xc0086924U,&c);number("ifconf",r,r<0?err():c.length);if(r<0 || c.length<0 || (u32)c.length>=sizeof(buffer))return -1;bytes("ifconf_bytes",buffer,(u32)c.length);int found=0;u32 off=0;
 while(off<(u32)c.length){if((u32)c.length-off<32)return -1;u8*row=buffer+off;u32 size=16+(row[16]>16?row[16]:16);if(size>(u32)c.length-off)return -1;u8 flags[32]={0};for(u32 i=0;i<16;i++)flags[i]=row[i];r=api_ioctl(fd,0xc0206911U,flags);number("ifflags",r,r<0?err():(flags[16]|flags[17]<<8));bytes("ifname",row,16);if(r==0 && (flags[16]&1) && row[17]==2 && row[16]>=8 && row[20]==127 && row[21]==0 && row[22]==0 && row[23]==1)found=1;off+=size;}
 return found;
}
static int routes(void){u8 data[4096];u32 n=sizeof(data);int mib[6]={4,17,0,2,1,0};int r=api_sysctl(mib,6,data,&n,0,0);number("route_sysctl",r,r<0?err():(int)n);if(r<0 || n>sizeof(data))return 0;bytes("route_bytes",data,n);return 1;}
static int run(void){int server=-1,client=-1,accepted=-1,result=70;struct address a={16,2,{0x46,0xa1},{127,0,0,1},{0}};logline("LOOPBACK_BEGIN_V1\n");server=api_socket(2,1,0);number("socket_server",server,server<0?err():0);if(server<0)goto done;
 int ready=0;for(int k=0;k<30;k++){number("readiness_attempt",k,0);int state=interfaces(server);if(state<0)goto done;if(state==1){ready=1;break;}if(k==0)routes();if(k<29){int r=api_usleep(1000000);int e=r<0?err():0;number("readiness_sleep",r,e);if(r<0 && e!=4)goto done;}}
 if(!ready){logline("LOOPBACK_NOT_READY_V1\n");goto done;}if(!routes() || !nonblock(server))goto done;
 int r=api_bind(server,&a,sizeof(a));number("bind",r,r<0?err():0);if(r<0)goto done;struct address actual={0};u32 n=sizeof(actual);r=api_getsockname(server,&actual,&n);number("bound_name",r,r<0?err():(int)n);bytes("bound_address",&actual,sizeof(actual));if(r<0||n!=16||!equal(&actual,&a,8))goto done;
 r=api_listen(server,1);number("listen",r,r<0?err():0);if(r<0)goto done;client=api_socket(2,1,0);number("socket_client",client,client<0?err():0);if(client<0||!nonblock(client))goto done;r=api_connect(client,&a,sizeof(a));int e=r<0?err():0;number("connect",r,e);if(r<0 && e!=36)goto done;if(r<0){if(!waitfd(client,4))goto done;int status=-1;n=4;r=api_getsockopt(client,0xffff,0x1007,&status,&n);number("connect_status",r,r<0?err():status);if(r<0||n!=4||status)goto done;}
 if(!waitfd(server,1))goto done;struct address peer={0};n=sizeof(peer);accepted=api_accept(server,&peer,&n);number("accept",accepted,accepted<0?err():(int)n);bytes("peer_address",&peer,sizeof(peer));if(accepted<0||n!=16||peer.family!=2||!equal(peer.ip,a.ip,4)||!nonblock(accepted))goto done;
 if(!send_all(client,request,sizeof(request)-1))goto done;char received[sizeof(response)];if(!recv_exact(accepted,received,sizeof(request)-1,0)||!equal(received,request,sizeof(request)-1))goto done;bytes("actual_request",received,sizeof(request)-1);
 if(!send_all(accepted,response,sizeof(response)-1))goto done;r=api_close(accepted);accepted=-1;number("close_response",r,r<0?err():0);if(r<0)goto done;if(!recv_exact(client,received,sizeof(response)-1,1)||!equal(received,response,sizeof(response)-1))goto done;bytes("actual_response",received,sizeof(response)-1);result=0;
 done: if(accepted>=0){int r=api_close(accepted);number("close_accepted",r,r<0?err():0);if(r<0)result=70;}if(client>=0){int r=api_close(client);number("close_client",r,r<0?err():0);if(r<0)result=70;}if(server>=0){int r=api_close(server);number("close_server",r,r<0?err():0);if(r<0)result=70;}
 if(log_failures)result=71;logline(result==0?"LOOPBACK_PASS_V1\n":"LOOPBACK_FAIL_V1\n");return result;
}
void start(void){int result=run();api__exit(result);}
