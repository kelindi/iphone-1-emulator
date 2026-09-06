#include "consume.h"
#include <string.h>
void consume_begin(consume_observer*s,const uint8_t*bytes){memset(s,0,sizeof *s);memcpy(s->expected,bytes,CONSUME_BYTES);s->active=true;}
consume_result consume_read(consume_observer*s,uint32_t a,unsigned width,uint32_t value,bool returned,unsigned owner){
 if(!s||!s->active||!returned)return CONSUME_IGNORE;
 if(width!=1&&width!=2&&width!=4)return CONSUME_REJECT;
 uint64_t end=(uint64_t)a+width,limit=(uint64_t)CONSUME_BASE+CONSUME_BYTES;
 if(end<=CONSUME_BASE||a>=limit)return CONSUME_IGNORE;
 if(owner||s->reads>=CONSUME_RECORD_CAP)return CONSUME_REJECT;
 /* Validate the whole overlapping span before recording any byte. Bytes
  * outside the PCM range, including unaligned edge reads, never count. */
 for(unsigned i=0;i<width;i++){
  uint64_t pa=(uint64_t)a+i;if(pa<CONSUME_BASE||pa>=limit)continue;
  unsigned offset=(unsigned)(pa-CONSUME_BASE);
  if((uint8_t)(value>>(8*i))!=s->expected[offset])return CONSUME_REJECT;
 }
 for(unsigned i=0;i<width;i++){
  uint64_t pa=(uint64_t)a+i;if(pa<CONSUME_BASE||pa>=limit)continue;
  unsigned offset=(unsigned)(pa-CONSUME_BASE);
  s->seen[offset]=(uint8_t)(value>>(8*i));if(!s->coverage[offset]){s->coverage[offset]=1;s->covered++;}
 }
 s->reads++;return CONSUME_ACCEPT;
}
bool consume_complete(const consume_observer*s){
 if(!s||!s->active||s->covered!=CONSUME_BYTES)return false;
 for(unsigned i=0;i<CONSUME_BYTES;i++)if(s->coverage[i]!=1||s->seen[i]!=s->expected[i])return false;
 return true;
}
bool consume_released(uint32_t address,unsigned width,uint32_t value,unsigned before,unsigned after){
 return address==0x2200f208u&&width==2&&value==1&&before==0&&after==1;
}
