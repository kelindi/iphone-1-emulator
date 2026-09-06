/* This retained retirement-wrapper unit stubs the new destination gate,
 * which has its own real-MMU positive/negative test. No guest evidence. */
#define transport_export synthetic_transport_export
#define destination_finish synthetic_destination_finish
#define main isolated_guest_main
#include "probe.c"
#undef main
#undef transport_export
#undef destination_finish
bool synthetic_destination_finish(destination_observer*o,const uint8_t*r,uint32_t b,uint32_t n,const uint8_t*e,uint64_t c){(void)o;(void)r;(void)b;(void)n;(void)e;(void)c;return true;}
bool synthetic_transport_export(const s5l8900_t*m,const destination_observer*d,const char*p,const char*s,bool f){(void)m;(void)d;(void)p;(void)s;(void)f;return true;}
static unsigned checks,forwarded;
static void forbidden_write(void*c,uint32_t a,uint32_t v){(void)c;(void)a;(void)v;forwarded++;}
static uint32_t forbidden_read(void*c,uint32_t a){(void)c;(void)a;forwarded++;return 0;}
static uint32_t fake_clock(void*ctx,uint32_t address){(void)ctx;(void)address;return 0;}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"service:%u %s\n",__LINE__,#x);exit(1);}}while(0)
static amc_engine before_engine;static amc_sram_t before_sram;
static void reject_boundary(const char*why){(void)why;before_engine=engine;before_sram=sram;result=0;
 if(!setjmp(finish)){service_boundary();CHECK(false);}CHECK(result==1);CHECK(!memcmp(&engine,&before_engine,sizeof engine));CHECK(!memcmp(&sram,&before_sram,sizeof sram));}
int main(void){
 output=".context/amc-first/stage-v/service-artifacts";
 uint8_t pcm[CONSUME_BYTES];for(unsigned i=0;i<sizeof pcm;i++)pcm[i]=(uint8_t)i;
 consume_begin(&consumed,pcm);engine_started=first_saved=true;engine.sram=&sram;engine.idle=&idle;
 engine.published_frames=2;engine.next_output=0;engine.service=true;engine.queue_used=10;
 sram.bytes[AMC_HEADER+8]=1;reject_boundary("fresh T prefix cannot be skipped");CHECK(service_calls==0);
 /* A genuine release is tested before the same third-work opportunity. */
 release_pending=release_path=ipc_complete=output_ack=output_enabled=service_requested=true;
 release_pc=0xc02726a8;release_cycles=101;release_cpsr=ARM_MODE_SVC;release_instruction=0xe1c030b0;machine.cpu.cycles=100;
 current_step=(guest_step){.cycles=100,.pc=release_pc,.cpsr=release_cpsr,.returned=true};machine.cpu.r[15]=release_pc+4;machine.cpu.cpsr=release_cpsr;
 for(unsigned i=0;i<sizeof pcm;i++)CHECK(consume_read(&consumed,CONSUME_BASE+i,1,pcm[i],true,0)==CONSUME_ACCEPT);
 memcpy(sram.bytes+AMC_OUTPUT0,pcm,sizeof pcm);reject_boundary("unretired store");CHECK(service_calls==0);
 machine.cpu.cycles=101;in_guest=true;reject_boundary("still inside guest");in_guest=false;
 consumed.coverage[7]=0;reject_boundary("missing read");consumed.coverage[7]=1;
 result=0;service_boundary();CHECK(result==6);CHECK(service_calls==0);CHECK(engine.published_frames==2);CHECK(machine.cpu.cycles==101);
 /* Exercise the real wrapper's admitted store, rather than setting its flag. */
 original.read32=fake_clock;enabled=true;idle.enabled=idle.de_complete=true;engine.initialized=true;
 release_pending=false;result=0;machine.cpu.cycles=200;machine.cpu.r[15]=0xc02726a8;machine.cpu.cpsr=ARM_MODE_SVC;
 writebus(&machine,0x2200f208,2,1);CHECK(!release_pending); /* idempotent1→1 */
 uint32_t opcode=0xe1c030b0;machine.ram=(uint8_t*)&opcode;machine.ram_base=release_pc;machine.ram_size=4;
 current_step=(guest_step){.cycles=199,.pc=release_pc,.cpsr=ARM_MODE_SVC,.active=true};in_guest=true;
 sram.bytes[AMC_HEADER+8]=0;writebus(&machine,0x2200f208,2,1);
 CHECK(release_pending&&release_cycles==200&&release_pc==0xc02726a8);CHECK(sram.bytes[AMC_HEADER+8]==1);CHECK(engine.host_writes[AMC_HEADER+8]);
 reject_boundary("actual store still unretired");in_guest=false;current_step.active=false;current_step.returned=true;machine.cpu.r[15]=release_pc+4;result=0;service_boundary();CHECK(result==6&&service_calls==0&&engine.published_frames==2);
 /* A fourth request is rejected before device/SRAM/ownership mutation. */
 third_input_cycles=9873362515ull;before_engine=engine;before_sram=sram;result=0;
 if(!setjmp(finish)){writebus(&machine,0x3850100c,4,1);CHECK(false);}
 CHECK(result==1);CHECK(!memcmp(&engine,&before_engine,sizeof engine));CHECK(!memcmp(&sram,&before_sram,sizeof sram));CHECK(service_calls==0);
 /* Actual interposed FIFO handler must stop before its original bus callback. */
 prefix_saved=true;original.write32=forbidden_write;original.read32=forbidden_read;before_engine=engine;before_sram=sram;
 if(!setjmp(finish)){writebus(&machine,0x3ca00010,4,123);CHECK(false);}CHECK(!forwarded);CHECK(!memcmp(&engine,&before_engine,sizeof engine)&&!memcmp(&sram,&before_sram,sizeof sram));
 if(!setjmp(finish)){(void)readbus(&machine,0x3cd00038,4);CHECK(false);}CHECK(!forwarded);CHECK(!memcmp(&engine,&before_engine,sizeof engine)&&!memcmp(&sram,&before_sram,sizeof sram));
 /* Current service false must not be confused with historical service_requested. */
 release_pending=false;engine.service=false;engine.events=0x200;engine.dma_active=false;engine.queue_used=10;engine.next_output=0;service_requested=true;
 sram.bytes[AMC_HEADER+16]=0x10;sram.bytes[AMC_HEADER+17]=0;sram.bytes[AMC_HEADER+18]=0x10;sram.bytes[AMC_HEADER+19]=0x10;
 streamed.count=2;unsigned old_calls=service_calls;result=0;service_boundary();CHECK(service_calls==old_calls+1&&engine.published_frames==2);
 engine.service=true;reject_boundary("fresh T gate only after real service");CHECK(service_calls==old_calls+1);
 printf("retirement/refill wrapper: %u checks passed; synthetic bytes, no guest execution\n",checks);return 0;
}
