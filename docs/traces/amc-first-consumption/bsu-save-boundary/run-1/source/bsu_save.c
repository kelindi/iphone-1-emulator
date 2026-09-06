#include "bsu_save.h"
#include <string.h>
static unsigned half(const amc_engine*e,unsigned n){return e->sram->bytes[AMC_HEADER+n]|(unsigned)e->sram->bytes[AMC_HEADER+n+1]<<8;}
static bool eligible(const amc_engine*e){
 if(!e||!e->initialized||!e->configured||e->fault||!e->idle||!e->idle->enabled||!e->idle->de_complete||!e->sram||!e->codec||!e->codec->at_frame_boundary||!e->decoder||!e->queue||!e->generation||e->generation==UINT64_MAX||!e->decoded_frames||!e->bsu_locked||!e->dma_paused||e->transport_enabled||e->setup_phase!=4||e->next_output>1||e->queue_used>AMC_QUEUE_BYTES||e->copied_bytes<e->decoded_bytes||e->copied_bytes-e->decoded_bytes!=e->queue_used)return false;
 if(e->dma_active&&(!e->dma_words||e->dma_words>AMC_QUEUE_BYTES/4||e->copied_words>=e->dma_words||(e->dma_start&3)||(uint64_t)e->dma_start+4ull*e->dma_words>UINT32_MAX))return false;
 if(half(e,16)!=0x10||half(e,18)!=0x1010||half(e,8)>1||half(e,10)>1||half(e,12)>4096||half(e,14)>4096||(half(e,12)&1)||(half(e,14)&1))return false;
 return e->codec->at_frame_boundary(e->decoder);
}
bool bsu_save_valid(const bsu_save*s,const amc_engine*e){
 return s&&s->owned&&s->identity==e&&eligible(e)&&!memcmp(&s->engine,e,sizeof *e)&&!memcmp(&s->idle,e->idle,sizeof s->idle)&&!memcmp(&s->sram,e->sram,sizeof s->sram)&&!memcmp(s->queue,e->queue,e->queue_used);
}
bool bsu_save_read(bsu_save*s,const amc_engine*e,uint32_t off,unsigned width,uint32_t*out){
 if(!s||!out||width!=4||!eligible(e))return false;
 if(!s->owned){
  if(s->phase||off!=0x410)return false;
  /* All checks precede mutation; ownership binds actual live allocations. */
  s->identity=e;s->engine=*e;s->idle=*e->idle;s->sram=*e->sram;memcpy(s->queue,e->queue,e->queue_used);
  s->words[0]=1; /* Explicit HLE encoding version, not silicon register identity. */
  s->words[1]=(uint32_t)e->generation;s->words[2]=(uint32_t)(e->generation>>32);
  s->words[3]=(uint32_t)e->decoded_bytes;s->words[4]=(uint32_t)(e->decoded_bytes>>32);
  s->words[5]=(uint32_t)e->copied_bytes;s->words[6]=(uint32_t)(e->copied_bytes>>32);s->owned=true;
 }else if(!bsu_save_valid(s,e))return false;
 uint32_t value;
 if(s->phase<7){if(off!=0x410+4*s->phase)return false;value=s->words[s->phase];}
 else if(s->phase==7){if(off!=0x188)return false;value=0;/* Actual adapter has no in-flight frame bit reader. */}
 else if(s->phase==8){if(off!=0x404)return false;value=0;/* Completed seven-word/count transaction only. */}
 else return false;
 *out=value;s->phase++;return true;
}
