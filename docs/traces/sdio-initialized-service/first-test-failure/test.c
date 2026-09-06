#include "runtime.h"
#include "checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static unsigned checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);}}while(0)
static const uint8_t *dma(void*ctx,uint32_t a,size_t n){(void)a;(void)n;return ctx;}
static uint8_t*read_file(const char*name,size_t*n){FILE*f=fopen(name,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);if(size<0){fclose(f);return NULL;}uint8_t*b=malloc((size_t)size);if(!b){fclose(f);return NULL;}*n=(size_t)size;if(fread(b,1,*n,f)!=*n){free(b);b=NULL;}fclose(f);return b;}
static sdio_t*fixture(void){size_t n=0;uint8_t*b=read_file(".context/sdio-first/checkpoint-v2/full-attempt-1/full-sdio.side",&n);sdio_t*s=calloc(1,sizeof *s);s5l_mtz1_t z={0};checkpoint_instant_t at={0};CHECK(b&&s);if(!b||!s)exit(2);CHECK(checkpoint_bundle_load(s,&z,&at,b,n));free(b);s->dma_reader=dma;s->dma_ctx=s;return s;}
static bool unchanged(const sdio_t*a,const sdio_t*b){return !memcmp(a,b,sizeof *a);}
static void rejected(sdio_t*s){runtime_t r={0},before=r;sdio_t*copy=malloc(sizeof *copy);memcpy(copy,s,sizeof *s);CHECK(!runtime_prepare(&r,s,s->now,NULL,NULL,NULL,true));CHECK(!memcmp(&r,&before,sizeof r));CHECK(unchanged(s,copy));free(copy);}
static uint32_t raw_register(sdio_t*s,unsigned address){uint32_t v=SDIO_R5_COMMAND_STATE;CHECK(runtime_register(s,0x10000000u|(address<<9),address,&v));return v;}
/* Actual delayed CMD52 engine, not direct-register-only lifecycle assertions. */
static uint32_t command(sdio_t*s,unsigned address,uint64_t now){
 CHECK(sdio_write(s,SDIO_ACK,UINT32_MAX,4,now));
 CHECK(sdio_write(s,SDIO_ARG,0x10000000u|(address<<9),4,now));
 CHECK(sdio_write(s,SDIO_CMD,0x80050074u,4,now));
 CHECK(s->busy);uint64_t count=s->completions;
 sdio_step(s,now+s->config.command_ticks-1);CHECK(s->busy&&s->completions==count);
 sdio_step(s,now+s->config.command_ticks);CHECK(!s->busy&&s->completions==count+1);
 CHECK(s->events&SDIO_DONE);return sdio_read(s,SDIO_RESP0,4,s->now);
}
typedef struct {unsigned calls,frees,fail_at;bool alias;void*first;} alloc_t;
static void*allocate(void*ctx,size_t n){alloc_t*a=ctx;a->calls++;if(a->calls==a->fail_at)return NULL;if(a->alias&&a->first)return a->first;void*p=malloc(n);if(!a->first)a->first=p;return p;}
static void release(void*ctx,void*p){alloc_t*a=ctx;a->frees++;free(p);}
static void activate(runtime_t*r,sdio_t*s){CHECK(runtime_prepare(r,s,s->now,NULL,NULL,NULL,true));sdio_step(s,s->now+RUNTIME_DELAY);CHECK(runtime_ready(r));}
int main(void){
 sdio_t*base=fixture();CHECK(runtime_image_supported(base));
 /* Independent exact supported-state admission, including zero-valued bytes. */
 for(unsigned bank=0;bank<5;bank++){
  size_t n=bank==0?sizeof base->main_model.low:bank==1?sizeof base->main_model.high:bank==2?sizeof base->main_model.low_coverage:bank==3?sizeof base->main_model.high_coverage:sizeof base->main_model.pending;
  for(size_t position=0;position<n;position+=n/7+1){
   sdio_t*s=malloc(sizeof *s);*s=*base;uint8_t*p=bank==0?s->main_model.low:bank==1?s->main_model.high:bank==2?s->main_model.low_coverage:bank==3?s->main_model.high_coverage:s->main_model.pending;
   p[position]^=1;rejected(s);free(s);
  }
 }
 #define NEGATE(statement) do{sdio_t*s=malloc(sizeof *s);*s=*base;statement;rejected(s);free(s);}while(0)
 NEGATE(s->helper.memory[0]^=1);NEGATE(s->helper.terminated=false);NEGATE(s->helper.framing_stalled=true);NEGATE(s->helper.used--);
 NEGATE(s->main_model.headers--);NEGATE(s->main_model.bodies--);NEGATE(s->main_model.copied--);NEGATE(s->main_model.phase=MAIN_HEADER);
 NEGATE(s->main_model.header[0]=1);NEGATE(s->main_model.header[1]=4);NEGATE(s->main_model.header[2]=4);NEGATE(s->main_model.header[3]^=1);
 NEGATE(s->main_model.low_after[0]=0);NEGATE(s->main_model.high_before[15]=0);NEGATE(s->main_model.errors=1);NEGATE(s->main_model.pending_length=31);
 NEGATE(s->endpoint.used=1);NEGATE(s->endpoint.interrupt_mask=1);NEGATE(s->endpoint.interrupt_pending=1);NEGATE(s->endpoint.ready=false);
 NEGATE(s->endpoint.enabled=false);NEGATE(s->endpoint.host_power=false);NEGATE(s->card.state=CARD_STANDBY);NEGATE(s->config.io_port++);
 NEGATE(s->busy=true);NEGATE(s->data.active=true);NEGATE(s->data.armed=true);NEGATE(s->irq_pending=1);NEGATE(s->events=0x20);
 NEGATE(s->dma_reader=NULL);NEGATE(s->data_completions--);NEGATE(s->now=UINT64_MAX-599);NEGATE(s->have_time=false);
 /* Transactional real allocation failure and accidental duplicate ownership. */
 for(unsigned failure=1;failure<=2;failure++){runtime_t r={0};sdio_t*s=malloc(sizeof *s);*s=*base;alloc_t a={.fail_at=failure};CHECK(!runtime_prepare(&r,s,s->now,allocate,release,&a,true));CHECK(a.calls==failure&&a.frees==failure-1);CHECK(!r.device&&!r.slots[0].allocation&&!r.slots[1].allocation);CHECK(unchanged(s,base));free(s);}
 {runtime_t r={0};alloc_t a={.alias=true};CHECK(!runtime_prepare(&r,base,base->now,allocate,release,&a,true));CHECK(a.calls==2&&a.frees==1);CHECK(!r.device);}
 /* Read-independent 600-tick initialization and frozen HANDOFF receive closure. */
 {sdio_t*s=fixture();runtime_t r={0};uint64_t start=s->now;sdio_t*saved=malloc(sizeof *saved);*saved=*s;
  CHECK(runtime_prepare(&r,s,start,NULL,NULL,NULL,true));CHECK(unchanged(s,saved));
  for(unsigned i=0;i<100;i++){CHECK(raw_register(s,0x20)==0x1008);CHECK(raw_register(s,0x34)==0x1000);CHECK(!runtime_ready(&r));}
  sdio_step(s,start+599);CHECK(!runtime_ready(&r));sdio_step(s,start+600);CHECK(runtime_ready(&r));CHECK(raw_register(s,0x20)==0x1009);
  CHECK(raw_register(s,0x34)==0x10dc);CHECK(raw_register(s,0x35)==0x10fe);
  uint8_t frame[512]={0};CHECK(!sdio_endpoint_can_receive(s,0x10000,32));CHECK(!sdio_endpoint_receive(s,0x10000,frame,32));CHECK(s->main_model.phase==MAIN_HANDOFF&&s->data_completions==504);
  uint32_t value=SDIO_R5_COMMAND_STATE;CHECK(!runtime_register(s,0,6,&value));CHECK(!runtime_register(s,0,0x40,&value));CHECK(value==SDIO_R5_COMMAND_STATE);
  CHECK(runtime_register(s,0x900068ff,0x34,&value));CHECK(value&SDIO_R5_ERROR);
  runtime_destroy(&r);free(saved);free(s);
 }
 /* Actual independent low/high publication boundaries. Extend only declared
  * private policy deadline here, so first completion precedes initialization. */
 for(unsigned order=0;order<2;order++){sdio_t*s=fixture();runtime_t r={0};CHECK(runtime_prepare(&r,s,s->now,NULL,NULL,NULL,true));r.deadline=s->now+1200;
  unsigned first=order?0x35:0x34,second=order?0x34:0x35;
  uint32_t a=command(s,first,s->now),b=command(s,second,s->now);CHECK(a==0x1000);CHECK(b==(order?0x10dc:0x10fe));CHECK(((a&255)|((b&255)<<8))!=0xfedc);
  CHECK(command(s,0x34,s->now)==0x10dc);CHECK(command(s,0x35,s->now)==0x10fe);CHECK(command(s,6,s->now)&SDIO_R5_ADDRESS);
  runtime_destroy(&r);free(s);
 }
 /* No marker mode is a distinct later verification negative. */
 {sdio_t*s=fixture();runtime_t r={0};CHECK(runtime_prepare(&r,s,s->now,NULL,NULL,NULL,false));sdio_step(s,s->now+600);CHECK(raw_register(s,0x20)==0x1009);CHECK(command(s,0x34,s->now)==0x1000);CHECK(command(s,0x35,s->now)==0x1000);runtime_destroy(&r);free(s);}
 /* Real arbitrary bytes, finite reserve/commit/read/release and stale tokens. */
 for(unsigned seed=0;seed<24;seed++){sdio_t*s=fixture();runtime_t r={0};activate(&r,s);uint8_t input[320],output[322];for(unsigned i=0;i<320;i++)input[i]=(uint8_t)(seed*43+i*17);
  for(unsigned channel=0;channel<2;channel++){uint64_t token=0;CHECK(!runtime_reserve(&r,channel,319,&token));CHECK(!token);CHECK(runtime_reserve(&r,channel,320,&token));CHECK(token);uint64_t second=123;CHECK(!runtime_reserve(&r,channel,320,&second));CHECK(second==123);
   CHECK(!runtime_commit(&r,channel,token+1,input,320));CHECK(!runtime_commit(&r,channel,token,input,319));CHECK(runtime_commit(&r,channel,token,input,320));CHECK(!runtime_commit(&r,channel,token,input,320));
   for(unsigned offset=0;offset<=320;offset++){memset(output,0xa5,sizeof output);CHECK(runtime_read(&r,channel,token,offset,output+1,320-offset));CHECK(!memcmp(output+1,input+offset,320-offset));CHECK(output[0]==0xa5&&output[321-offset]==0xa5);}
   CHECK(!runtime_read(&r,channel,token,319,output,2));CHECK(!runtime_read(&r,channel,token,SIZE_MAX,output,1));CHECK(!runtime_cancel(&r,channel,token));
   runtime_t copy=r;uint8_t bytes[352];memcpy(bytes,r.slots[channel].allocation,352);CHECK(!runtime_execute_command(&r));CHECK(!memcmp(&r,&copy,sizeof r));CHECK(!memcmp(bytes,r.slots[channel].allocation,352));
   CHECK(runtime_release(&r,channel,token));CHECK(!runtime_release(&r,channel,token));CHECK(runtime_reserve(&r,channel,320,&second));CHECK(second!=token);CHECK(!runtime_commit(&r,channel,token,input,320));CHECK(runtime_cancel(&r,channel,second));
  }
  runtime_destroy(&r);free(s);
 }
 /* Controller reset/rebind preserve completed ownership and card deadline. */
 for(unsigned kind=0;kind<3;kind++){sdio_t*s=fixture();runtime_t r={0};activate(&r,s);uint8_t data[320],out[320];memset(data,0x57,320);uint64_t token=0;CHECK(runtime_reserve(&r,0,320,&token));CHECK(runtime_commit(&r,0,token,data,320));s->events|=0x20;s->irq_pending|=1;
  if(kind==0)sdio_host_reset(s);else if(kind==1)sdio_bind_dma_reader(s,dma,&r);else CHECK(sdio_write(s,SDIO_DCTRL,3,4,s->now));
  CHECK(runtime_ready(&r));CHECK(runtime_read(&r,0,token,0,out,320));CHECK(!memcmp(data,out,320));if(kind==1)CHECK((s->events&0x20)&&(s->irq_pending&1));else CHECK(!(s->events&0x20)&&!(s->irq_pending&1));
  CHECK(runtime_release(&r,0,token));runtime_destroy(&r);free(s);
 }
 /* Card-only unavailability cancels uncommitted reservations, retains bytes;
  * full card reset explicitly clears both. No stale deadline resumes service. */
 for(unsigned kind=0;kind<5;kind++){sdio_t*s=fixture();runtime_t r={0};activate(&r,s);uint8_t bytes[320]={7},out[320];uint64_t a=0,b=0;CHECK(runtime_reserve(&r,0,320,&a));CHECK(runtime_commit(&r,0,a,bytes,320));CHECK(runtime_reserve(&r,1,320,&b));
  if(kind==0)runtime_unavailable(s);else if(kind==1){s->endpoint.host_power=false;sdio_step(s,s->now);}else if(kind==2)sdio_step(s,s->now-1);else if(kind==3)sdio_card_reset(s);else{s->endpoint.enabled=false;sdio_step(s,s->now);}
  CHECK(!runtime_ready(&r));CHECK(!runtime_commit(&r,1,b,bytes,320));CHECK(!r.deadline);
  if(kind==3)CHECK(!runtime_read(&r,0,a,0,out,320));else{CHECK(runtime_read(&r,0,a,0,out,320));CHECK(!memcmp(out,bytes,320));}
  sdio_step(s,s->now+100000);CHECK(!runtime_ready(&r));runtime_destroy(&r);free(s);
 }
 /* Invalid capacity/canary while reserved must fail closed without using the
  * corrupted capacity as a memset bound. Deliberately negative host fixtures. */
 for(unsigned kind=0;kind<3;kind++){sdio_t*s=fixture();runtime_t r={0};activate(&r,s);uint64_t token=0;CHECK(runtime_reserve(&r,0,320,&token));if(kind==0)r.slots[0].capacity=SIZE_MAX;else if(kind==1)r.slots[0].allocation[0]=0;else r.slots[1].allocation[351]=0;sdio_step(s,s->now);CHECK(!runtime_ready(&r));CHECK(r.phase==RT_INVALID);runtime_destroy(&r);free(s);}
 /* Disabled endpoint remains byte-compatible: original real command sequence. */
 {sdio_t*s=fixture();CHECK(command(s,0x20,s->now)==0x1008);CHECK(command(s,0x34,s->now)==0x1000);free(s);}
 free(base);printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
