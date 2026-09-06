#include "amc_suspend.h"
#include <string.h>
bool amc_suspend_quiet(const amc_suspend*s,const amc_engine*e){
 return s&&e&&s->owned&&!e->initialized&&!e->configured&&!e->decoder&&!e->queue&&!e->service&&!e->dma_active&&!e->input_pending&&!e->events&&!e->mask&&!e->input_mask&&e->idle&&!e->idle->enabled&&!e->idle->de_complete;
}
bool amc_suspend_valid(const amc_suspend*s,const bsu_save*b,const amc_engine*e){
 if(!s||!b||!e||!s->owned||b->phase!=9||!b->owned||b->identity!=e||!amc_suspend_quiet(s,e)||memcmp(e,&s->vacant,sizeof *e)||memcmp(e->idle,&s->vacant_idle,sizeof s->vacant_idle))return false;
 if(!s->retained.decoder||!s->retained.queue||!s->retained.codec||!s->retained.codec->at_frame_boundary||!s->retained.codec->at_frame_boundary(s->retained.decoder)||s->retained.idle!=&s->retained_idle)return false;
 /* Restore only a private pointer for comparison with the original identity.
  * The retained resource itself always uses its retained control snapshot. */
 amc_engine normalized=s->retained;normalized.idle=b->engine.idle;
 return !memcmp(&normalized,&b->engine,sizeof normalized)&&!memcmp(&s->retained_idle,&b->idle,sizeof b->idle)&&!memcmp(s->retained.sram,&b->sram,sizeof b->sram)&&!memcmp(s->retained.queue,b->queue,b->engine.queue_used);
}
bool amc_suspend_write(amc_suspend*s,bsu_save*b,amc_engine*e,uint32_t clock,uint32_t power,uint32_t address,unsigned width,uint32_t value,amc_gate_writer writer,void*ctx){
 if(!s||!b||!e||!writer||width!=4||s->phase>=4)return false;
 uint32_t next_clock=clock,next_power=power;
 if(s->phase<2){if(s->owned||b->phase!=9||!bsu_save_valid(b,e)||(clock&0x18)||(power&0x13))return false;}
 else if(!amc_suspend_valid(s,b,e))return false;
 if(s->phase&&(clock!=s->clock||power!=s->power))return false;
 switch(s->phase){
 case 0:if(address!=0x39a0000c||value!=3)return false;next_power=power&~3u;break;
 case 1:if(address!=0x39a00010||value!=0x10)return false;next_power=power|0x10;break;
 case 2:if(address!=0x3c500048||value!=(clock|8u)||(clock&0x18)||!(power&0x10))return false;next_clock=value;break;
 case 3:if(address!=0x3c500048||value!=(clock|0x10u)||(clock&0x18)!=8||!(power&0x10))return false;next_clock=value;break;
 }
 /* No allocation/fallible operation after admission. The supplied writer is
  * the existing synchronous controller bus write, called exactly once. */
 writer(ctx,address,value);
 if(s->phase==1){
  s->retained=*e;s->retained_idle=*e->idle;s->retained.idle=&s->retained_idle;
  amc_idle_t*idle=e->idle;amc_sram_t*sram=e->sram;uint64_t generation=e->generation;
  /* Transfer, never destroy, the only decoder/queue allocation ownership. */
  amc_engine_init(e);e->generation=generation;e->idle=idle;e->sram=sram;
  amc_idle_dependencies(idle,next_clock,next_power);s->owned=true;s->vacant=*e;s->vacant_idle=*idle;
 }
 s->clock=next_clock;s->power=next_power;s->phase++;return true;
}
void amc_suspend_destroy(amc_suspend*s){
 if(!s||!s->owned)return;amc_engine_destroy(&s->retained);s->owned=false;
}
