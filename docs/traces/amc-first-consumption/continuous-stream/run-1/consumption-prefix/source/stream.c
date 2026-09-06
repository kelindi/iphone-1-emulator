#include "stream.h"
#include <string.h>
static unsigned half(const amc_engine*e,unsigned o){return e->sram->bytes[o]|(unsigned)e->sram->bytes[o+1]<<8;}
bool stream_prepare(const stream_observer*o,const amc_engine*e){
 if(!o||!e||o->count!=e->published_frames||e->next_output>1)return false;
 if(!e->configured||!e->service||!e->queue_used||(e->events&0x400)||!half(e,AMC_HEADER+8+2*e->next_output))return true;
 if(o->count>=STREAM_CAP)return false;unsigned n=o->slot_latest[e->next_output];return !n||o->gen[n-1].released;
}
bool stream_publish(stream_observer*o,const amc_engine*e,unsigned slot,uint64_t cycles){
 if(!o||!e||slot>1||o->count>=STREAM_CAP||e->published_frames!=o->count+1||e->next_output!=(slot^1)||half(e,AMC_HEADER+8+2*slot)||half(e,AMC_HEADER+12+2*slot)!=2048)return false;
 unsigned old=o->slot_latest[slot];if(old&&!o->gen[old-1].released)return false;
 stream_generation*g=&o->gen[o->count];memset(g,0,sizeof*g);g->ordinal=o->count+1;g->slot=slot;g->published=cycles;g->decoded=e->decoded_bytes;g->copied=e->copied_bytes;g->samples=e->sample_units;g->events=e->events;g->mask=e->mask;consume_begin(&g->reads,e->sram->bytes+(slot?AMC_OUTPUT1:AMC_OUTPUT0));o->slot_latest[slot]=++o->count;return true;
}
bool stream_read(stream_observer*o,const amc_engine*e,uint32_t a,unsigned w,uint32_t v,uint32_t pc,uint32_t cpsr,uint64_t cycles){
 if(!o||!e)return false;if(w!=1&&w!=2&&w!=4)return false;
 for(unsigned slot=0;slot<2;slot++){uint32_t base=0x22000000u+(slot?AMC_OUTPUT1:AMC_OUTPUT0);if((uint64_t)a+w<=base||a>=base+4096)continue;
  unsigned n=o->slot_latest[slot];if(!n||o->read_count>=STREAM_READ_CAP)return false;stream_generation*g=&o->gen[n-1];if(g->released||g->pending)return false;
  unsigned owner=half(e,AMC_HEADER+8+2*slot);consume_result r=consume_read(&g->reads,CONSUME_BASE+(a-base),w,v,true,owner);if(r!=CONSUME_ACCEPT)return false;
  o->ledger[o->read_count++]=(stream_read_record){cycles,n,slot,a,w,v,pc,cpsr,owner};return true;
 }return true;
}
bool stream_release_begin(stream_observer*o,const amc_engine*e,uint32_t a,unsigned width,uint32_t value,const guest_step*s,uint64_t cycles,uint32_t pc,uint32_t cpsr,uint32_t opcode,uint32_t driver,uint32_t index,bool in_guest){
 if(!o||!e||o->pending_generation||!in_guest||width!=2||value!=1||a<0x2200f208||a>0x2200f20a||(a&1)||index>1||a!=0x2200f208+2*index||!driver)return false;
 unsigned n=o->slot_latest[index];if(!n)return false;stream_generation*g=&o->gen[n-1];
 if(g->released||g->pending||!g->ipc||!g->ack||!consume_complete(&g->reads)||half(e,AMC_HEADER+8+2*index)||!release_start_valid(s,cycles,pc,cpsr,opcode))return false;
 if(memcmp(g->reads.expected,e->sram->bytes+(index?AMC_OUTPUT1:AMC_OUTPUT0),4096))return false;
 g->pending=true;g->release_step=*s;g->release_callback=cycles;g->release_pc=pc;g->release_cpsr=cpsr;g->release_opcode=opcode;g->release_address=a;g->driver=driver;g->index=index;o->pending_generation=n;return true;
}
bool stream_release_finish(stream_observer*o,const amc_engine*e,const guest_step*s,uint64_t cycles,uint32_t pc,uint32_t cpsr,bool in_guest){
 if(!o||!e||!o->pending_generation||o->pending_generation>o->count)return false;stream_generation*g=&o->gen[o->pending_generation-1];
 if(!release_finish_valid(s,g->release_callback,g->release_pc,g->release_cpsr,g->release_opcode,cycles,pc,cpsr,in_guest)||s->cycles!=g->release_step.cycles||half(e,AMC_HEADER+8+2*g->slot)!=1)return false;
 g->pending=false;g->released=true;g->release_end=cycles;g->after_pc=pc;g->after_cpsr=cpsr;o->pending_generation=0;o->release_count++;return true;
}
void stream_ack(stream_observer*o,uint64_t cycles){if(o&&o->count){stream_generation*g=&o->gen[o->count-1];g->ack=true;g->ack_cycles=cycles;}}
bool stream_ipc(stream_observer*o,unsigned slot,uint64_t cycles,bool complete){
 if(!o)return false;
 if(!complete){if(slot>1||!o->slot_latest[slot]||o->ipc_pending)return false;unsigned n=o->slot_latest[slot];if(o->gen[n-1].released||o->gen[n-1].pending||o->gen[n-1].ipc||cycles<o->gen[n-1].published)return false;o->ipc_pending=n;return true;}
 if(!o->ipc_pending||o->ipc_pending>o->count)return false;stream_generation*g=&o->gen[o->ipc_pending-1];g->ipc=true;g->ipc_cycles=cycles;o->ipc_pending=0;return true;
}
bool stream_admit(const amc_engine*e,const amc_physical_ram*r,unsigned number,uint32_t pc,uint32_t lr,uint32_t cpsr){
 return e&&r&&number>=6&&number<=8&&pc==0xc0278130&&lr==0xc0274874&&(cpsr&63)==0x13&&e->initialized&&e->configured&&!e->fault&&!e->dma_active&&!e->input_pending&&e->input_phase==5&&e->transport_enabled&&e->staged_words==4096&&e->dma_words==4096&&e->copied_words==4096&&e->copied_bytes==16384ull*(number-1)&&e->copied_bytes>=e->decoded_bytes&&e->queue_used==e->copied_bytes-e->decoded_bytes&&e->queue_used<=65536&&e->published_frames<=64&&!(e->staged_address&3)&&e->staged_address>=r->base&&(uint64_t)e->staged_address+16384<=(uint64_t)r->base+r->size;
}
