#ifndef AMC_LATER_H
#define AMC_LATER_H
#include "capture.h"
#include <string.h>
/* Probe bounds only; the unchanged engine owns all actual DMA semantics. */
static bool admit_later(const amc_engine*s,const amc_physical_ram*r,unsigned number,
 uint64_t cycles,uint64_t first,uint32_t pc,uint32_t lr,uint32_t cpsr){
 size_t offset,bytes;
 if(!s||(number!=4&&number!=5)||first!=9873234585ull||pc!=0xc0278130||lr!=0xc0274874||
 (cpsr&0x3f)!=0x13||!s->initialized||!s->configured||s->fault||s->dma_active||s->input_pending||
 s->input_phase!=5||!s->transport_enabled||s->staged_words!=4096||s->dma_words!=4096||s->copied_words!=4096||
 s->copied_bytes!=16384ull*(number-1)||s->decoded_bytes>s->copied_bytes||s->queue_used!=s->copied_bytes-s->decoded_bytes||
 s->queue_used>AMC_QUEUE_BYTES||s->published_frames<1||s->published_frames>2||
 !capture_span(r,s->staged_address,s->staged_words,&offset,&bytes)||bytes!=16384)return false;
 if(number==5)return cycles>9873476740ull;
 return cycles==9873476740ull&&s->staged_address==0x14a8b000&&s->dma_start==0x14ae1000&&
 s->queue_used==48804&&s->decoded_bytes==348&&s->decoded_frames==2&&s->published_frames==1&&s->events==0x600&&s->mask==0x2000;
}
typedef struct {uint64_t cycles,copied_before,decoded_before,decoded_after;uint32_t request,address,bytes,offset,words_before,words_after,queue_before,queue_after,pending,active;} dma_record;
_Static_assert(sizeof(dma_record)==72,"fixed copy observer record");
typedef struct {dma_record records[1024];unsigned count,used;uint8_t bytes[81920],scratch[65536];} dma_observer;
static bool copy_prepare(dma_observer*o,const amc_engine*s,const amc_physical_ram*r,unsigned request,
 const uint8_t*capture,uint64_t cycles,dma_record*out){
 memset(out,0,sizeof *out);
 if(!s->dma_active||s->dma_paused)return true;
 if(!s->initialized||!s->idle||!s->idle->enabled||!s->idle->de_complete||s->fault||
 s->queue_used>AMC_QUEUE_BYTES||s->copied_words>s->dma_words||s->dma_words!=4096||request<4||request>8)return false;
 size_t words=(AMC_QUEUE_BYTES-s->queue_used)/4;
 if(words>1024)words=1024;if(words>s->dma_words-s->copied_words)words=s->dma_words-s->copied_words;
 if(!words)return true;
 size_t offset,bytes;uint64_t address=(uint64_t)s->dma_start+4ull*s->copied_words;
 if(address>UINT32_MAX||!capture_span(r,(uint32_t)address,words,&offset,&bytes)||o->count>=1024||bytes>sizeof o->bytes-o->used||
 !capture||memcmp(r->bytes+offset,capture+4*s->copied_words,bytes))return false;
 memcpy(o->scratch,s->queue,s->queue_used);memcpy(o->scratch+s->queue_used,r->bytes+offset,bytes);
 *out=(dma_record){.cycles=cycles,.copied_before=s->copied_bytes,.decoded_before=s->decoded_bytes,
 .request=request,.address=(uint32_t)address,.bytes=bytes,.offset=o->used,.words_before=s->copied_words,.queue_before=s->queue_used};
 return true;
}
static bool copy_finish(dma_observer*o,const amc_engine*s,dma_record*row){
 if(!row->bytes)return true;
 if(s->copied_bytes!=row->copied_before+row->bytes||s->copied_words!=row->words_before+row->bytes/4||
 s->decoded_bytes<row->decoded_before)return false;
 uint64_t consumed=s->decoded_bytes-row->decoded_before,total=row->queue_before+row->bytes;
 if(consumed>total||s->queue_used!=total-consumed||memcmp(s->queue,o->scratch+consumed,s->queue_used))return false;
 row->decoded_after=s->decoded_bytes;row->words_after=s->copied_words;row->queue_after=s->queue_used;
 row->pending=s->input_pending;row->active=s->dma_active;
 memcpy(o->bytes+o->used,o->scratch+row->queue_before,row->bytes);o->used+=row->bytes;o->records[o->count++]=*row;return true;
}
#endif
