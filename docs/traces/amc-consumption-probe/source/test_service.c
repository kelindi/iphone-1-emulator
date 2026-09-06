#define main isolated_guest_main
#include "probe.c"
#undef main
static unsigned checks;
static uint32_t fake_clock(void*ctx,uint32_t address){(void)ctx;(void)address;return 0;}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"service:%u %s\n",__LINE__,#x);exit(1);}}while(0)
static amc_engine before_engine;static amc_sram_t before_sram;
static void reject_boundary(const char*why){(void)why;before_engine=engine;before_sram=sram;result=0;
 if(!setjmp(finish)){service_boundary();CHECK(false);}CHECK(result==1);CHECK(!memcmp(&engine,&before_engine,sizeof engine));CHECK(!memcmp(&sram,&before_sram,sizeof sram));}
int main(void){
 uint8_t pcm[CONSUME_BYTES];for(unsigned i=0;i<sizeof pcm;i++)pcm[i]=(uint8_t)i;
 consume_begin(&consumed,pcm);engine_started=first_saved=true;engine.sram=&sram;engine.idle=&idle;
 engine.published_frames=2;engine.next_output=0;engine.service=true;engine.queue_used=10;
 sram.bytes[AMC_HEADER+8]=1;reject_boundary("third work cap");CHECK(service_calls==0);
 /* A genuine release is tested before the same third-work opportunity. */
 release_pending=release_path=ipc_complete=output_ack=output_enabled=service_requested=true;
 release_pc=0xc02726a8;release_cycles=100;machine.cpu.cycles=100;
 for(unsigned i=0;i<sizeof pcm;i++)CHECK(consume_read(&consumed,CONSUME_BASE+i,1,pcm[i],true,0)==CONSUME_ACCEPT);
 memcpy(sram.bytes+AMC_OUTPUT0,pcm,sizeof pcm);reject_boundary("unretired store");CHECK(service_calls==0);
 machine.cpu.cycles=101;in_guest=true;reject_boundary("still inside guest");in_guest=false;
 consumed.coverage[7]=0;reject_boundary("missing read");consumed.coverage[7]=1;
 result=0;service_boundary();CHECK(result==6);CHECK(service_calls==0);CHECK(engine.published_frames==2);CHECK(machine.cpu.cycles==101);
 /* Exercise the real wrapper's admitted store, rather than setting its flag. */
 original.read32=fake_clock;enabled=true;idle.enabled=idle.de_complete=true;engine.initialized=true;
 release_pending=false;result=0;machine.cpu.cycles=200;machine.cpu.r[15]=0xc02726a8;machine.cpu.cpsr=ARM_MODE_SVC;
 writebus(&machine,0x2200f208,2,1);CHECK(!release_pending); /* idempotent1→1 */
 sram.bytes[AMC_HEADER+8]=0;writebus(&machine,0x2200f208,2,1);
 CHECK(release_pending&&release_cycles==200&&release_pc==0xc02726a8);CHECK(sram.bytes[AMC_HEADER+8]==1);CHECK(engine.host_writes[AMC_HEADER+8]);
 reject_boundary("actual store still unretired");machine.cpu.cycles=201;result=0;service_boundary();CHECK(result==6&&service_calls==0&&engine.published_frames==2);
 printf("retirement/refill wrapper: %u checks passed; synthetic bytes, no guest execution\n",checks);return 0;
}
