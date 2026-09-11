#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DNSServiceCreateConnection mock_create
#define DNSServiceRegisterRecord mock_register
#define DNSServiceQueryRecord mock_query
#define DNSServiceRefSockFD mock_fd
#define DNSServiceProcessResult mock_process
#define DNSServiceRefDeallocate mock_free
#define poll mock_poll
#define write mock_write
#define usleep mock_sleep
#define __error mock_error
#define _exit mock_exit
#include "dns_record_probe.c"
static unsigned checks,mode,creates,sleeps,freed[4],registered_pending,polls;
static int handles[4],rec_handle,error_value;
static RegisterReply register_cb;
static QueryReply query_cb;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"dns:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int mock_create(Service*out){creates++;if(mode==7)return -65563;*out=&handles[0];return 0;}
int mock_register(Service ref,Record*out,u32 flags,u32 iface,const char*full,u16 type,u16 klass,u16 n,const void*data,u32 ttl,RegisterReply cb,void*context){
 CHECK(ref==&handles[0]&&flags==0x20&&iface==0xffffffffu&&strcmp(full,"iphone-wu.apple.com.")==0);
 CHECK(type==1&&klass==1&&n==4&&memcmp(data,"\177\0\0\1",4)==0&&ttl==60&&context==&record);
 register_cb=cb;registered_pending=1;*out=&rec_handle;return 0;
}
int mock_query(Service*out,u32 flags,u32 iface,const char*full,u16 type,u16 klass,QueryReply cb,void*context){
 CHECK(query.role>=1&&query.role<=3&&flags==0x1000&&type==1&&klass==1&&context==&query&&strcmp(full,"iphone-wu.apple.com")==0);
 CHECK(iface==(query.role==3?0xffffffffu:0));
 if(mode==5&&query.role==1)return -65554;
 *out=&handles[query.role];query_cb=cb;return 0;
}
int mock_fd(Service ref){for(unsigned i=0;i<4;i++)if(ref==&handles[i])return (int)i+10;CHECK(0);return -1;}
int mock_poll(struct pollitem*p,u32 n,int timeout){CHECK(n>=1&&n<=2&&timeout==100);polls++;int ready=0;for(unsigned i=0;i<n;i++){p[i].revents=0;if((p[i].fd==10&&registered_pending)||(p[i].fd!=10&&mode!=4)){p[i].revents=1;ready++;}}return ready;}
int mock_process(Service ref){
 if(ref==&handles[0]){CHECK(registered_pending);registered_pending=0;register_cb(ref,record,0,mode==3?-65548:0,&record);return 0;}
 CHECK(ref==query.service);
 if((query.role==1&&mode!=1)||(query.role==2&&mode==2))query_cb(ref,0,0,-65554,0,0,0,0,0,0,&query);
 else{u8 bytes[4]={127,0,0,mode==6?2:1};query_cb(ref,2,0xffffffffu,0,"iphone-wu.apple.com.",1,1,4,bytes,60,&query);}
 return 0;
}
void mock_free(Service ref){for(unsigned i=0;i<4;i++)if(ref==&handles[i]){CHECK(freed[i]++==0);return;}CHECK(0);}
int mock_write(int fd,const void*p,u32 n){CHECK(fd==1&&p&&n<=256);return (int)n;}
int mock_sleep(u32 n){CHECK(n==1000000);sleeps++;return 0;}
int*mock_error(void){return &error_value;}
void mock_exit(int result){(void)result;CHECK(0);}
int main(void){
 for(mode=0;mode<8;mode++){
  creates=sleeps=registered_pending=polls=0;memset(freed,0,sizeof freed);registration=0;record=0;reg_done=reg_good=log_failed=callback_bad=0;query=(struct query){0};
  int rc=run();CHECK(rc==((mode==0||mode==5)?0:(mode==3||mode==7)?70:71));
  CHECK(!registration&&!query.service&&!record);
  if(mode==7){CHECK(creates==30&&sleeps==29&&freed[0]==0);}
  else{CHECK(creates==1&&sleeps==0&&freed[0]==1);CHECK(freed[1]==(mode==5?0:1));if(mode!=3)CHECK(freed[2]==1&&freed[3]==1);}
  CHECK(polls<=200);
 }
 printf("PASS %u DNS-SD ownership/options/control/failure/bounds checks; host mocks, no guest\n",checks);
}
