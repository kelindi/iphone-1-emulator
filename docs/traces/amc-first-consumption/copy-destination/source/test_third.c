#include "admission.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static amc_engine e,copy;
static amc_sram_t mem;
static uint8_t bytes[16384],queue[AMC_QUEUE_BYTES];
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
#define ALLOWED() admit_third(&e,9873362515ull,9873234585ull,0xc0278130,0xc0274874,0x13)
int main(void){
 e.initialized=e.configured=e.transport_enabled=true;e.input_phase=5;e.staged_address=0x14ae1000;e.staged_words=4096;e.dma_start=0x14ac1000;e.dma_words=e.copied_words=4096;e.copied_bytes=32768;e.queue_used=32420;e.decoded_bytes=348;e.decoded_frames=2;e.published_frames=1;e.events=0x600;e.mask=0x2000;
 CHECK(ALLOWED());copy=e;
#define NEG(field,value) do{e.field=value;CHECK(!ALLOWED());e=copy;}while(0)
 NEG(dma_active,true);NEG(input_pending,true);NEG(staged_address,0x14ae1004);NEG(staged_words,4095);NEG(input_phase,6);NEG(queue_used,65536);NEG(events,0x200);NEG(published_frames,2);NEG(fault,true);NEG(initialized,false);NEG(configured,false);NEG(transport_enabled,false);NEG(dma_start,0x14ac1004);NEG(dma_words,4095);NEG(copied_words,4095);NEG(copied_bytes,32764);NEG(decoded_bytes,347);NEG(decoded_frames,3);NEG(mask,0x2400);
 CHECK(!admit_third(NULL,9873362515ull,9873234585ull,0xc0278130,0xc0274874,0x13));CHECK(!admit_third(&e,9873362515ull,9873234586ull,0xc0278130,0xc0274874,0x13));CHECK(!admit_third(&e,9873362515ull,9873234585ull,0xc0278130,0xc0274878,0x13));CHECK(!memcmp(&e,&copy,sizeof e));
 CHECK(!admit_third(&e,9873362516ull,9873234585ull,0xc0278130,0xc0274874,0x13));CHECK(!admit_third(&e,9873362515ull,9873234585ull,0xc0278134,0xc0274874,0x13));
 CHECK(!admit_third(&e,9873362515ull,9873234585ull,0xc0278130,0xc0274874,0x33));CHECK(!admit_third(&e,9873362515ull,9873234585ull,0xc0278130,0xc0274874,0x12));
 amc_idle_t idle={0};idle.enabled=idle.de_complete=true;e.idle=&idle;e.sram=&mem;e.queue=queue;e.setup_phase=4;e.service=true;e.input_mask=1;
 mem.bytes[AMC_HEADER+8]=0;mem.bytes[AMC_HEADER+10]=1;mem.bytes[AMC_HEADER+16]=0x10;mem.bytes[AMC_HEADER+18]=0x10;mem.bytes[AMC_HEADER+19]=0x10;
 for(unsigned i=0;i<sizeof bytes;i++)bytes[i]=(uint8_t)(i*7);memset(queue,0x99,32420);
 amc_physical_ram ram={bytes,0x14ae1000,sizeof bytes};
 CHECK(amc_engine_write(&e,&ram,0x100c,4,1)==AMC_ENGINE_OK);CHECK(e.dma_active&&e.copied_words==0&&e.copied_bytes==32768&&e.queue_used==32420);
 copy=e;CHECK(amc_engine_write(&e,&ram,0x100c,4,0)==AMC_ENGINE_REJECT);CHECK(!memcmp(&e,&copy,sizeof e));CHECK(amc_engine_write(&e,&ram,0x1000,4,1)==AMC_ENGINE_REJECT);CHECK(!memcmp(&e,&copy,sizeof e));
 for(unsigned i=1;i<=4;i++){CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(e.copied_words==1024*i&&e.copied_bytes==32768+4096*i&&e.queue_used==32420+4096*i);CHECK(e.input_pending==(i==4));CHECK(e.dma_active==(i!=4));CHECK(!memcmp(queue+32420,bytes,4096*i));uint32_t current;CHECK(amc_engine_read(&e,0x1010,4,&current)==AMC_ENGINE_OK&&current==ram.base+4096*i);CHECK(e.decoded_bytes==348&&e.published_frames==1&&e.events==0x600);}
 CHECK(amc_engine_irq(&e));CHECK(amc_engine_write(&e,&ram,0x101c,4,1)==AMC_ENGINE_OK&&!e.input_pending);
 e.input_phase=5;e.queue_used=AMC_QUEUE_BYTES;CHECK(amc_engine_write(&e,&ram,0x100c,4,1)==AMC_ENGINE_OK);copy=e;CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(!memcmp(&e,&copy,sizeof e));
 printf("third input admission/DMA: %u checks passed; synthetic live RAM only\n",checks);
}
