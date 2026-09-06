#include "later.h"
#include "lock-receipt.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks,probes,decodes;static uint8_t q[65536],input[16384];static amc_sram_t sram;static amc_engine e,prior;static amc_idle_t idle;static dma_observer ledger;static unsigned history;
#define CHECK(x) do{checks++;assert(x);}while(0)
static bool probe(const uint8_t*b,size_t n,unsigned rate,amc_decoded*d){(void)b;(void)n;(void)rate;(void)d;probes++;return false;}
static bool decode(void*c,const uint8_t*b,size_t n,amc_decoded*d){(void)c;(void)b;(void)n;(void)d;decodes++;return false;}
static amc_codec_ops codec={.probe=probe,.decode=decode};static amc_physical_ram ram={input,0x14ae1000,sizeof input};
static void init(void){memset(&e,0,sizeof e);memset(&sram,0,sizeof sram);memset(&idle,0,sizeof idle);memset(&ledger,0,sizeof ledger);idle.enabled=idle.de_complete=true;e.initialized=e.configured=e.service=e.dma_active=e.transport_enabled=true;e.idle=&idle;e.sram=&sram;e.codec=&codec;e.decoder=&history;e.queue=q;e.setup_phase=4;e.staged_address=e.dma_start=ram.base;e.dma_words=e.staged_words=4096;e.copied_words=346;e.copied_bytes=83304;e.decoded_bytes=17770;e.queue_used=65534;e.events=0x200;e.mask=0x2000;e.input_mask=1;e.published_frames=53;e.decoded_frames=54;e.sample_units=108544;sram.bytes[AMC_HEADER+8]=sram.bytes[AMC_HEADER+10]=1;sram.bytes[AMC_HEADER+16]=0x10;sram.bytes[AMC_HEADER+18]=0x10;sram.bytes[AMC_HEADER+19]=0x10;memset(q,0x20,sizeof q);for(unsigned i=0;i<sizeof input;i++)input[i]=(uint8_t)(i*7);}
static void reject(unsigned off,unsigned w,unsigned v){prior=e;uint8_t before[65536];memcpy(before,q,sizeof q);amc_sram_t saved=sram;CHECK(amc_engine_write(&e,&ram,off,w,v)==AMC_ENGINE_REJECT);CHECK(!memcmp(&e,&prior,sizeof e)&&!memcmp(q,before,sizeof q)&&!memcmp(&sram,&saved,sizeof sram));}
int main(void){
 init();reject(0x1000,4,0);reject(0xa00,4,0);reject(0x400,2,1);reject(0x400,4,0);reject(0x400,4,2);
 #define BAD(field,val) do{init();e.field=val;reject(0x400,4,1);}while(0)
 BAD(decoder,NULL);BAD(queue,NULL);BAD(codec,NULL);BAD(configured,false);BAD(queue_used,65537);BAD(copied_words,4097);BAD(dma_start,ram.base+4);BAD(next_output,2);
 init();sram.bytes[AMC_HEADER+8]=2;reject(0x400,4,1);init();CHECK(amc_engine_write(&e,&ram,0x400,4,1)==AMC_ENGINE_OK&&e.bsu_locked);prior=e;CHECK(amc_engine_write(&e,&ram,0x400,4,1)==AMC_ENGINE_OK&&!memcmp(&e,&prior,sizeof e));
 reject(0xa00,4,0);reject(0x400,4,0);reject(0xc,4,1);CHECK(amc_engine_write(&e,&ram,0x1000,4,0)==AMC_ENGINE_OK&&e.dma_paused&&e.dma_active&&!e.input_pending);CHECK(amc_engine_write(&e,&ram,0xa00,4,0)==AMC_ENGINE_OK&&!e.transport_enabled);
 uint32_t start=0,current=0,count=0,locked=0;CHECK(amc_engine_read(&e,0x400,4,&locked)==AMC_ENGINE_OK&&locked==1);CHECK(amc_engine_read(&e,0x1004,4,&start)==AMC_ENGINE_OK&&start==ram.base);CHECK(amc_engine_read(&e,0x1010,4,&current)==AMC_ENGINE_OK&&current==ram.base+1384);CHECK(amc_engine_read(&e,0x1008,4,&count)==AMC_ENGINE_OK&&count-((current-start)>>2)==3750);
 prior=e;amc_sram_t saved=sram;uint8_t before[65536];memcpy(before,q,sizeof q);
 for(unsigned i=0;i<100;i++){dma_record row;CHECK(copy_prepare(&ledger,&e,&ram,6,input,i,&row)&&!row.bytes);CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(copy_finish(&ledger,&e,&row));CHECK(!memcmp(&e,&prior,sizeof e)&&!memcmp(&sram,&saved,sizeof sram)&&!memcmp(q,before,sizeof q)&&!probes&&!decodes);}
 CHECK(amc_engine_write(&e,&ram,0x1000,4,0)==AMC_ENGINE_OK&&!memcmp(&e,&prior,sizeof e));CHECK(amc_engine_write(&e,&ram,0xa00,4,0)==AMC_ENGINE_OK&&!memcmp(&e,&prior,sizeof e));reject(0x1004,4,ram.base);reject(0x1008,4,4096);reject(0x100c,4,1);reject(0x410,4,0);
 uint32_t sentinel=0xabcdef12;CHECK(amc_engine_read(&e,0x410,4,&sentinel)==AMC_ENGINE_REJECT&&sentinel==0xabcdef12);CHECK(amc_engine_write(&e,&ram,0xb0c,4,0x400)==AMC_ENGINE_OK&&(e.mask&0x400));CHECK(amc_engine_write(&e,&ram,0xb04,4,0x200)==AMC_ENGINE_OK&&!e.events);CHECK(amc_engine_write(&e,&ram,0x101c,4,1)==AMC_ENGINE_OK&&!e.input_pending);
 /* Lock alone must still allow a real DMA copy if capacity is available. */
 init();e.decoded_bytes+=4096;e.queue_used-=4096;CHECK(amc_engine_write(&e,&ram,0x400,4,1)==AMC_ENGINE_OK);dma_record row;CHECK(copy_prepare(&ledger,&e,&ram,6,input,1,&row)&&row.bytes==4096);CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT&&e.copied_words==1370&&!probes&&!decodes);CHECK(copy_finish(&ledger,&e,&row)&&ledger.used==4096);CHECK(!memcmp(ledger.bytes,input+1384,4096));CHECK(amc_engine_write(&e,&ram,0x1000,4,0)==AMC_ENGINE_OK);
 for(unsigned i=0;i<3;i++){uint32_t pc=i==0?0xc0277ce8:i==1?0xc0278218:0xc0278220,a=i==0?0x38500400:i==1?0x38501000:0x38500a00;guest_step step={.cycles=100,.pc=pc,.cpsr=0x60000013,.active=true};lock_receipt r={0};CHECK(!lock_begin(&r,&step,101,pc,step.cpsr,lock_opcode(pc)^1,a,i==0));CHECK(lock_begin(&r,&step,101,pc,step.cpsr,lock_opcode(pc),a,i==0));CHECK(!lock_finish(&r,&step,101,pc+4,step.cpsr,true));step.active=false;step.returned=true;CHECK(!lock_finish(&r,&step,101,pc+8,step.cpsr,false));CHECK(!lock_finish(&r,&step,101,pc+4,step.cpsr^32,false));CHECK(!lock_finish(&r,&step,101,0x18,0x92,false));CHECK(lock_finish(&r,&step,101,pc+4,step.cpsr,false));}
 printf("PASS %u lock/resource/partial-DMA/ledger/retirement checks; synthetic states only\n",checks);
}
