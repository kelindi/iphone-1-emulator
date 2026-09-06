#define main retained_lock_main
#include "test_lock.c"
#undef main
#include "amc_suspend.h"
#include "gate_receipt.h"
static bsu_save saved;static amc_suspend suspended,old_suspend;static amc_codec_ops ops;
static unsigned writes,closed,freed;static uint32_t clock_word,power_word;static bool boundary=true;
static bool frame(const void*p){return p==&history&&boundary;}
static void close_decoder(void*p){CHECK(p==&history);closed++;}
static void free_queue(void*p){CHECK(p==q);freed++;}
static void writer(void*c,uint32_t a,uint32_t v){CHECK(c==&writes);writes++;if(a==0x39a0000c)power_word&=~v;else if(a==0x39a00010)power_word|=v;else if(a==0x3c500048)clock_word=v;else CHECK(0);}
static void setup(void){init();memset(&saved,0,sizeof saved);memset(&suspended,0,sizeof suspended);ops=codec;ops.at_frame_boundary=frame;ops.close=close_decoder;e.codec=&ops;e.release=free_queue;e.generation=1;e.bsu_locked=e.dma_paused=true;e.transport_enabled=false;boundary=true;writes=closed=freed=0;clock_word=0x4fffe267;power_word=0x12ec;uint32_t out;for(unsigned i=0;i<7;i++)CHECK(bsu_save_read(&saved,&e,0x410+4*i,4,&out));CHECK(bsu_save_read(&saved,&e,0x188,4,&out));CHECK(bsu_save_read(&saved,&e,0x404,4,&out));}
static bool wr(uint32_t a,unsigned w,uint32_t v){return amc_suspend_write(&suspended,&saved,&e,clock_word,power_word,a,w,v,writer,&writes);}
static void bad(uint32_t a,unsigned w,uint32_t v){old_suspend=suspended;prior=e;amc_idle_t old_idle=idle;unsigned n=writes;uint32_t c=clock_word,p=power_word;CHECK(!wr(a,w,v));CHECK(!memcmp(&suspended,&old_suspend,sizeof suspended)&&!memcmp(&e,&prior,sizeof e)&&!memcmp(&idle,&old_idle,sizeof idle)&&writes==n&&c==clock_word&&p==power_word);}
static void stage(unsigned n){setup();if(n>0)CHECK(wr(0x39a0000c,4,3));if(n>1)CHECK(wr(0x39a00010,4,0x10));if(n>2)CHECK(wr(0x3c500048,4,clock_word|8));if(n>3)CHECK(wr(0x3c500048,4,clock_word|0x10));}
int main(void){
 for(unsigned phase=0;phase<5;phase++){
  stage(phase);bad(0x3c500050,4,0xa000);bad(0x39a0000c,2,3);bad(0x39a00010,4,0);bad(0x3c500048,4,clock_word^0x10000);bad(0x38500410,4,1);
  if(phase!=0)bad(0x39a0000c,4,3);if(phase!=1)bad(0x39a00010,4,0x10);
  if(phase>=2){CHECK(amc_suspend_valid(&suspended,&saved,&e)&&amc_suspend_quiet(&suspended,&e));CHECK(!amc_engine_irq(&e)&&suspended.retained.decoder==&history&&suspended.retained.queue==q&&suspended.retained.queue_used==65534&&suspended.retained.dma_active);CHECK(!closed&&!freed);}
 }
 for(unsigned changed=0;changed<9;changed++)for(unsigned phase=0;phase<4;phase++){
  stage(phase);switch(changed){case 0:boundary=false;break;case 1:q[10]^=1;break;case 2:sram.bytes[99]^=1;break;case 3:saved.phase=8;break;case 4:e.generation++;break;case 5:idle.divider++;break;case 6:power_word^=1;break;case 7:clock_word^=8;break;case 8:if(phase>=2)suspended.retained.events^=0x400;else e.events^=0x400;break;}
  bad(phase==0?0x39a0000c:phase==1?0x39a00010:0x3c500048,4,phase==0?3:phase==1?0x10:clock_word|(phase==2?8:0x10));
 }
 stage(4);CHECK(clock_word==0x4fffe27f&&power_word==0x12fc&&writes==4);CHECK(amc_suspend_valid(&suspended,&saved,&e));amc_engine_destroy(&e);CHECK(!closed&&!freed);amc_suspend_destroy(&suspended);CHECK(closed==1&&freed==1);amc_suspend_destroy(&suspended);CHECK(closed==1&&freed==1);
 for(unsigned i=0;i<4;i++){guest_step step={.cycles=100,.pc=0xc0411f3c,.cpsr=0x60000013,.active=true};gate_receipt r={0};CHECK(!gate_begin(&r,&step,101,step.pc,step.cpsr^32,0xe7832001,1,1));CHECK(!gate_begin(&r,&step,101,step.pc,step.cpsr,0xe7932001,1,1));CHECK(gate_begin(&r,&step,101,step.pc,step.cpsr,0xe7832001,1,1));CHECK(!gate_finish(&r,&step,101,step.pc+4,step.cpsr,true));step.active=false;step.returned=true;CHECK(!gate_finish(&r,&step,101,step.pc+8,step.cpsr,false));CHECK(!gate_finish(&r,&step,101,step.pc+4,step.cpsr^32,false));CHECK(!gate_finish(&r,&step,101,0x18,0x92,false));CHECK(gate_finish(&r,&step,101,step.pc+4,step.cpsr,false));}
 printf("PASS %u suspension ownership/order/atomic rejection/conservation/retirement checks\n",checks);
}
