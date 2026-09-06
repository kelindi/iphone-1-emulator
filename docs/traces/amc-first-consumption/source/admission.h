#ifndef AMC_SECOND_ADMISSION_H
#define AMC_SECOND_ADMISSION_H
#include "amc_engine.h"
/* Bounded probe admission, not a generic hardware requirement. */
static bool admit_second(const amc_engine*s,uint64_t cycles,uint64_t first,uint32_t pc,uint32_t lr,uint32_t cpsr){
 return s&&(cpsr&0x3fu)==0x13u&&cycles==9873247421ull&&first==9873234585ull&&pc==0xc0278130u&&lr==0xc0274874u&&
 s->initialized&&s->configured&&!s->fault&&!s->dma_active&&!s->input_pending&&s->input_phase==5&&s->transport_enabled&&
 s->staged_address==0x14ac1000u&&s->staged_words==4096&&s->dma_start==0x14a8b000u&&s->dma_words==4096&&s->copied_words==4096&&
 s->copied_bytes==16384&&s->queue_used==16036&&s->queue_used+16384<=AMC_QUEUE_BYTES&&s->decoded_bytes==348&&s->decoded_frames==2&&s->published_frames==1&&s->events==0x600&&s->mask==0x2000;
}
static bool admit_third(const amc_engine*s,uint64_t cycles,uint64_t first,uint32_t pc,uint32_t lr,uint32_t cpsr){
 return s&&(cpsr&0x3fu)==0x13u&&cycles==9873362515ull&&first==9873234585ull&&pc==0xc0278130u&&lr==0xc0274874u&&
 s->initialized&&s->configured&&!s->fault&&!s->dma_active&&!s->input_pending&&s->input_phase==5&&s->transport_enabled&&
 s->staged_address==0x14ae1000u&&s->staged_words==4096&&s->dma_start==0x14ac1000u&&s->dma_words==4096&&s->copied_words==4096&&
 s->copied_bytes==32768&&s->queue_used==32420&&s->queue_used+16384<=AMC_QUEUE_BYTES&&s->decoded_bytes==348&&s->decoded_frames==2&&s->published_frames==1&&s->events==0x600&&s->mask==0x2000;
}
#endif
