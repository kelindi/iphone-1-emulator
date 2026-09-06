/* Isolated observational replay. No instruction/register/date replacement. */
#include "emu_api.c"
#include "peek.h"
#include <inttypes.h>
static bool (*original_write)(void*,uint8_t);
static unsigned date_writes,records;
static bool observation_failed,callback_returned,staged;
static uint32_t staged_pc,expected_pc,expected_mode;
static uint64_t staged_cycles;
static unsigned retired_boundaries;
static bool peek(uint32_t va,void*out,size_t n){
 return amc_peek_ram(&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,va,out,n);
}
static void bytes_at(const char*label,uint32_t va,size_t n){
 uint8_t b[16];if(n>sizeof b||!peek(va,b,n)){printf("PEEK unresolved %s address%08x bytes%zu\n",label,va,n);observation_failed=true;return;}
 printf("PEEK %s address%08x bytes",label,va);for(size_t i=0;i<n;i++)printf("%02x",b[i]);puts("");
}
static bool observe_write(void*ctx,uint8_t value){
 s5l_pcf50635_t*p=ctx;bool date=p->have_ptr&&p->ptr>=0x59&&p->ptr<=0x5f;uint8_t reg=p->ptr;bool accepted=original_write(ctx,value);
 if(date){date_writes++;printf("RTC write ordinal%u register%02x value%02x accepted%u cycles%"PRIu64" ticks%"PRIu64" pc%08x cpsr%08x\n",date_writes,reg,value,accepted,machine.cpu.cycles,machine.timer.ticks,machine.cpu.r[15],machine.cpu.cpsr);}
 return accepted;
}
static void observe_boundary(void){
 struct target {uint32_t pc;bool thumb;const char*name;};
 static const struct target targets[]={
  {0xc01031f0,true,"settimeofday"},{0xc001898c,true,"clock_set_calendar"},
  {0xc014c674,true,"platform_set_gmt"},{0xc015c490,true,"arm_platform_set_gmt"},
  {0xc015c4b0,true,"rtc_resource_return"},{0xc015de04,true,"rtc_platform_dispatch"},
  {0xc03ff964,false,"rtc_set_gmt"},{0xc03ff978,false,"rtc_set_date"},
  {0xc03ffa20,false,"queue_date"},{0xc03ffa60,false,"async_callback"},
  {0xc03ffb0c,false,"pmu_write_call"},{0xc03ffb10,false,"pmu_write_return"},
  {0xc03ffb28,false,"callback_exit"}};
 arm_cpu_t*c=&machine.cpu;
 for(unsigned i=0;i<sizeof targets/sizeof targets[0];i++){
  const struct target*t=&targets[i];if(c->r[15]!=t->pc)continue;
  if((c->cpsr&31)!=ARM_MODE_SVC||!!(c->cpsr&32)!=t->thumb){observation_failed=true;printf("ISA mismatch pc%08x cpsr%08x\n",c->r[15],c->cpsr);return;}
  staged=true;staged_pc=t->pc;staged_cycles=c->cycles;
  expected_pc=t->pc+(t->thumb?2:4);expected_mode=c->cpsr&0x3f;
  if(t->pc==0xc03ffb0c){uint32_t target;if(!peek(c->r[12]+0x4fc,&target,4)){observation_failed=true;return;}expected_pc=target&~1u;expected_mode=(expected_mode&~32u)|((target&1u)?32u:0u);}
  if(++records>64){observation_failed=true;puts("record cap reached");return;}
  printf("OBSERVED pre-step %s cycles%"PRIu64" ticks%"PRIu64" pc%08x cpsr%08x lr%08x r0%08x r1%08x r2%08x r3%08x\n",t->name,c->cycles,machine.timer.ticks,c->r[15],c->cpsr,c->r[14],c->r[0],c->r[1],c->r[2],c->r[3]);
  if(t->pc==0xc01031f0){uint32_t uap[2];if(!peek(c->r[1],uap,sizeof uap)){observation_failed=true;return;}bytes_at("timeval",uap[0],8);}
  if(t->pc==0xc03ff978||t->pc==0xc03ffa20)bytes_at("date-structure",c->r[1],10);
  if(t->pc==0xc03ffa60)bytes_at("pending-date-and-flag",c->r[0]+0x94,10);
  if(t->pc==0xc03ffb0c){if(c->r[1]!=0x59||c->r[3]!=7){observation_failed=true;return;}bytes_at("actual-pmu-write",c->r[2],7);}

 }
}
static bool hook(void*ctx){(void)ctx;observe_boundary();return false;}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 machine_ready=s5l_board_init(s5l_board_find("m68"),&machine,256u<<20);
 if(!machine_ready||snapshot_load(&machine,argv[1])!=SNAP_OK)return 3;
 if(machine.cpu.cycles!=12498269215ull||machine.timer.ticks!=5055447112ull||machine.pmu.unknown_writes)return 4;
 machine.mtz1.uptime_ms=machine.timer.ticks/6000;machine.mtz1.uptime_tick_acc=machine.timer.ticks%6000;
 reset_host_inputs();emu_release_inputs();
 s5l_i2c_slave_t*s=&machine.i2c[0].slaves[0];if(s->addr!=PCF50635_I2C_ADDR||s->ctx!=&machine.pmu)return 5;
 original_write=s->write;s->write=observe_write;
 const uint32_t targets[]={0xc01031f0,0xc001898c,0xc014c674,0xc015c490,0xc015c4b0,0xc015de04,0xc03ff964,0xc03ff978,0xc03ffa20,0xc03ffa60,0xc03ffb0c,0xc03ffb10,0xc03ffb28};
 if(!s5l8900_set_pre_step_hook(&machine,hook,NULL,targets,sizeof targets/sizeof targets[0]))return 9;
 uint64_t start=machine.cpu.cycles;bool released=false;
 printf("START cycles%"PRIu64" ticks%"PRIu64" cap400000000; original before-picker export; tap160,416\n",start,machine.timer.ticks);
 if(emu_touch(1,160,416))return 6;
 for(unsigned n=0;n<400000000;n++){
  if(!released&&machine.cpu.cycles-start>=5000000){if(emu_touch(0,160,416))return 7;released=true;}
  uint64_t before=machine.cpu.cycles;emu_run(1);if(machine.cpu.cycles!=before+1)return 8;
  if(staged){
   bool retired=machine.cpu.cycles==staged_cycles+1&&machine.cpu.r[15]==expected_pc&&(machine.cpu.cpsr&0x3f)==expected_mode;
   printf("ORDER %s source%08x expected%08x actual%08x mode%02x cycles%"PRIu64"\n",retired?"retired":"unretired",staged_pc,expected_pc,machine.cpu.r[15],machine.cpu.cpsr&0x3f,machine.cpu.cycles);
   if(retired){retired_boundaries++;if(staged_pc==0xc03ffb28&&date_writes==7)callback_returned=true;}
   staged=false;
  }
  if(observation_failed||callback_returned)break;
 }
 printf("END retired%"PRIu64" records%u rtcWrites%u callbackExit%u observationFailed%u pc%08x unknownWrites%"PRIu64"\n",machine.cpu.cycles-start,records,date_writes,callback_returned,observation_failed,machine.cpu.r[15],machine.pmu.unknown_writes);
 release_machine();return !observation_failed&&callback_returned?0:10;
}
