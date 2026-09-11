/* Independently authored, finite original DNS-SD diagnostic. No gateway edits.
 * API ABI: Apple mDNSResponder108.4 dns_sd.h; exact original libSystem exports
 * independently audited. Query options mirror observed original Weather.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef void *Service;
typedef void *Record;
struct pollitem {int fd;short events,revents;};
typedef void (*RegisterReply)(Service,Record,u32,int,void*);
typedef void (*QueryReply)(Service,u32,u32,int,const char*,u16,u16,u16,const void*,u32,void*);
#define IMPORT(ret,name,args) extern ret name args; static ret (*volatile api_##name) args=name
IMPORT(int,DNSServiceCreateConnection,(Service*));
IMPORT(int,DNSServiceRegisterRecord,(Service,Record*,u32,u32,const char*,u16,u16,u16,const void*,u32,RegisterReply,void*));
IMPORT(int,DNSServiceQueryRecord,(Service*,u32,u32,const char*,u16,u16,QueryReply,void*));
IMPORT(int,DNSServiceRefSockFD,(Service));
IMPORT(int,DNSServiceProcessResult,(Service));
IMPORT(void,DNSServiceRefDeallocate,(Service));
IMPORT(int,poll,(struct pollitem*,u32,int));
IMPORT(int,write,(int,const void*,u32));
IMPORT(int,usleep,(u32));
IMPORT(int*,__error,(void));
IMPORT(void,_exit,(int));
static const char name[]="iphone-wu.apple.com";
static const u8 address[4]={127,0,0,1};
static Service registration;
static Record record;
static unsigned reg_done,reg_good,log_failed,callback_bad;
struct query {Service service;unsigned role,done,matched,callbacks;int error;};
static struct query query;
static unsigned size(const char*s){unsigned n=0;while(s[n]&&n<256)n++;return n;}
static int same(const u8*a,const u8*b,unsigned n){for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static void logline(const char*s){unsigned n=size(s),off=0;for(unsigned k=0;k<64&&off<n;k++){int r=api_write(1,s+off,n-off);if(r>0&&(unsigned)r<=n-off)off+=(unsigned)r;else if(r<0&&*api___error()==4)continue;else break;}if(off!=n)log_failed=1;}
static char hex(unsigned x){return "0123456789abcdef"[x&15];}
static void number(const char*label,u32 a,u32 b){char line[96];unsigned n=size(label);if(n>70){log_failed=1;return;}for(unsigned i=0;i<n;i++)line[i]=label[i];line[n++]=' ';for(int i=7;i>=0;i--)line[n++]=hex(a>>(4*i));line[n++]=' ';for(int i=7;i>=0;i--)line[n++]=hex(b>>(4*i));line[n++]='\n';line[n]=0;logline(line);}
static void payload(const void*data,unsigned count){const u8*p=data;for(unsigned off=0;off<count;){char b[72];unsigned n=0;b[n++]='D';b[n++]=' ';for(unsigned k=0;k<32&&off<count;k++,off++){b[n++]=hex(p[off]>>4);b[n++]=hex(p[off]);}b[n++]='\n';b[n]=0;logline(b);}}
static void registered(Service ref,Record rr,u32 flags,int error,void*context){
 number("DNS_REGISTER_CALLBACK",flags,(u32)error);
 if(ref!=registration||rr!=record||context!=&record){callback_bad=1;reg_done=1;return;}
 reg_done=1;reg_good=error==0;
}
static void answered(Service ref,u32 flags,u32 interface,int error,const char*full,u16 type,u16 klass,u16 bytes,const void*data,u32 ttl,void*context){
 number("DNS_QUERY_CALLBACK",query.role,(u32)error);number("DNS_QUERY_FLAGS_INTERFACE",flags,interface);
 if(ref!=query.service||context!=&query){callback_bad=1;query.done=1;return;}
 query.callbacks++;
 if(error){query.error=error;query.done=1;return;}
 number("DNS_QUERY_TYPE_CLASS",type,klass);number("DNS_QUERY_BYTES_TTL",bytes,ttl);
 if(full){unsigned n=size(full);number("DNS_QUERY_NAME_BYTES",n,0);payload(full,n);}
 if(bytes&&data)payload(data,bytes<256?bytes:256);
 if((flags&2)&&type==1&&klass==1&&bytes==4&&data&&same(data,address,4)){
  unsigned n=full?size(full):0;unsigned expected=size(name);
  if(n==expected||(n==expected+1&&full[expected]=='.')){
   if(same((const u8*)full,(const u8*)name,expected)){query.matched=1;query.done=1;}
  }
 }
}
/* Each phase: at most50 polls of100ms. Processing is invoked only on readable
 * original DNS-SD descriptors; synchronous library calls have no extra timeout
 * API, so the external native instruction/wall cap is still required. */
static int pump(int registration_only){
 for(unsigned i=0;i<50;i++){
  if(callback_bad)return 0;
  if(registration_only?reg_done:query.done)return 1;
  struct pollitem p[2];Service refs[2];unsigned count=0;
  if(registration){refs[count]=registration;p[count]=(struct pollitem){api_DNSServiceRefSockFD(registration),1,0};if(p[count].fd<0)return 0;count++;}
  if(!registration_only){refs[count]=query.service;p[count]=(struct pollitem){api_DNSServiceRefSockFD(query.service),1,0};if(p[count].fd<0)return 0;count++;}
  int result=api_poll(p,count,100);
  if(result<0){if(*api___error()==4)continue;number("DNS_POLL_ERROR",(u32)result,(u32)*api___error());return 0;}
  if(!result)continue;
  for(unsigned j=0;j<count;j++)if(p[j].revents){
   if(p[j].revents&0x38){number("DNS_POLL_EVENTS",p[j].revents,j);return 0;}
   if(p[j].revents&1){int rc=api_DNSServiceProcessResult(refs[j]);number("DNS_PROCESS_RESULT",j,(u32)rc);if(rc)return 0;}
  }
 }
 number("DNS_PHASE_TIMEOUT",registration_only?0:query.role,query.callbacks);return 1;
}
static int ask(unsigned role,u32 interface){
 query=(struct query){0};query.role=role;
 number("DNS_QUERY_BEGIN",role,interface);number("DNS_QUERY_OPTIONS",0x1000,1);
 int rc=api_DNSServiceQueryRecord(&query.service,0x1000,interface,name,1,1,answered,&query);
 number("DNS_QUERY_CALL",role,(u32)rc);if(rc){query.service=0;query.error=rc;query.done=1;number("DNS_QUERY_END",role,0);number("DNS_QUERY_STATUS",0,(u32)rc);return 1;}
 int ok=pump(0);number("DNS_QUERY_END",role,query.matched);number("DNS_QUERY_STATUS",query.callbacks,(u32)query.error);
 api_DNSServiceRefDeallocate(query.service);query.service=0;return ok;
}
static int run(void){
 logline("DNS_LOCAL_RECORD_PROBE_BEGIN_V1\n");int result=70,connected=0;
 for(unsigned i=0;i<30;i++){
  int rc=api_DNSServiceCreateConnection(&registration);number("DNS_CONNECT",i,(u32)rc);
  if(!rc){connected=1;break;}registration=0;
  if(i!=29){int sleep=api_usleep(1000000);if(sleep<0&&*api___error()!=4)goto done;}
 }
 if(!connected)goto done;
 if(!ask(1,0))goto done;unsigned before=query.matched;
 int rc=api_DNSServiceRegisterRecord(registration,&record,0x20,0xffffffffu,"iphone-wu.apple.com.",1,1,4,address,60,registered,&record);
 number("DNS_REGISTER_CALL",0x20,(u32)rc);if(rc)goto done;
 if(!pump(1)||!reg_done||!reg_good)goto done;
 if(!ask(2,0))goto done;unsigned after=query.matched;
 if(!ask(3,0xffffffffu))goto done;unsigned local=query.matched;
 number("DNS_ATTRIBUTION",before,after);number("DNS_LOCAL_CONTROL",local,callback_bad);
 result=!before&&after&&local&&!callback_bad?0:71;
 done:
 if(query.service){api_DNSServiceRefDeallocate(query.service);query.service=0;}
 if(registration){api_DNSServiceRefDeallocate(registration);registration=0;record=0;}
 if(log_failed)result=72;
 number("DNS_LOCAL_RECORD_PROBE_END_V1",(u32)result,log_failed);return log_failed?72:result;
}
void start(void){api__exit(run());}
