#include "later.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static amc_engine e,saved;static amc_idle_t idle;static amc_sram_t sram;
static dma_observer o;static uint8_t ram_bytes[32768],queue[65536];
static const int16_t pcm[2048]={0};
static bool probe(const uint8_t*b,size_t n,unsigned r,amc_decoded*d){(void)b;(void)r;if(n<4096)return false;*d=(amc_decoded){4096,2048,pcm};return true;}
static bool decode(void*c,const uint8_t*b,size_t n,amc_decoded*d){(void)c;(void)b;CHECK(n==4096);*d=(amc_decoded){4096,2048,pcm};return true;}
static const amc_codec_ops codec={.probe=probe,.decode=decode};
static amc_physical_ram ram={ram_bytes,0x14a8b000,sizeof ram_bytes};
#define ADMIT(n) admit_later(&e,&ram,n,(n)==4?9873476740ull:9873476800ull,9873234585ull,0xc0278130,0xc0274874,0x13)
int main(void){
 idle.enabled=idle.de_complete=true;e.idle=&idle;e.sram=&sram;e.queue=queue;e.codec=&codec;
 e.initialized=e.configured=e.transport_enabled=true;e.input_phase=5;e.staged_address=ram.base;e.staged_words=4096;e.dma_start=0x14ae1000;e.dma_words=e.copied_words=4096;e.copied_bytes=49152;e.queue_used=48804;e.decoded_bytes=348;e.decoded_frames=2;e.published_frames=1;e.events=0x600;e.mask=0x2000;e.setup_phase=4;
 CHECK(ADMIT(4));saved=e;
 #define NEG(field,value) do{e.field=value;CHECK(!ADMIT(4));e=saved;}while(0)
 NEG(dma_active,true);NEG(input_pending,true);NEG(input_phase,6);NEG(staged_address,ram.base+4);NEG(staged_words,4095);NEG(queue_used,48808);NEG(decoded_bytes,347);NEG(copied_bytes,49148);NEG(published_frames,2);NEG(events,0x200);NEG(mask,0x2400);NEG(fault,true);NEG(initialized,false);NEG(configured,false);NEG(transport_enabled,false);NEG(dma_words,0);NEG(copied_words,4095);NEG(dma_start,ram.base);
 for(unsigned mode=0;mode<64;mode++)if(mode!=0x13)CHECK(!admit_later(&e,&ram,4,9873476740ull,9873234585ull,0xc0278130,0xc0274874,mode));
 CHECK(!admit_later(&e,&ram,4,9873476741ull,9873234585ull,0xc0278130,0xc0274874,0x13));CHECK(!admit_later(&e,&ram,4,9873476740ull,9873234585ull,0xc0278134,0xc0274874,0x13));CHECK(!admit_later(&e,&ram,4,9873476740ull,9873234585ull,0xc0278130,0xc0274878,0x13));CHECK(!ADMIT(6));CHECK(!memcmp(&e,&saved,sizeof e));
 for(unsigned i=0;i<sizeof ram_bytes;i++)ram_bytes[i]=(uint8_t)(i*17+3);memset(queue,0x20,e.queue_used);sram.bytes[AMC_HEADER+10]=1;sram.bytes[AMC_HEADER+16]=0x10;sram.bytes[AMC_HEADER+18]=0x10;sram.bytes[AMC_HEADER+19]=0x10;
 CHECK(amc_engine_write(&e,&ram,0x100c,4,1)==AMC_ENGINE_OK);
 dma_record row;
 for(unsigned i=0;i<4;i++){CHECK(copy_prepare(&o,&e,&ram,4,ram_bytes,100+i,&row));CHECK(row.bytes==4096);CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(copy_finish(&o,&e,&row));}
 CHECK(e.queue_used==65188&&e.copied_bytes==65536&&!e.dma_active&&e.input_pending);CHECK(o.count==4&&o.used==16384&&!memcmp(o.bytes,ram_bytes,16384));
 CHECK(amc_engine_write(&e,&ram,0x101c,4,1)==AMC_ENGINE_OK);e.input_phase=5;e.staged_address=ram.base+16384;CHECK(ADMIT(5));
 saved=e;e.mask=0x2400;e.events=0x200;CHECK(ADMIT(5));e=saved;
 e.input_pending=true;CHECK(!ADMIT(5));e=saved;e.dma_active=true;CHECK(!ADMIT(5));e=saved;e.staged_address=0xfffffff0;CHECK(!ADMIT(5));e=saved;e.staged_words=0;CHECK(!ADMIT(5));e=saved;
 CHECK(amc_engine_write(&e,&ram,0x100c,4,1)==AMC_ENGINE_OK);CHECK(copy_prepare(&o,&e,&ram,5,ram_bytes+16384,200,&row));CHECK(row.bytes==348);CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(copy_finish(&o,&e,&row));
 CHECK(e.queue_used==65536&&e.copied_bytes==65884&&e.copied_words==87&&e.dma_active&&!e.input_pending);uint32_t current;CHECK(amc_engine_read(&e,0x1010,4,&current)==AMC_ENGINE_OK&&current==e.dma_start+348);saved=e;
 for(unsigned i=0;i<20;i++){CHECK(copy_prepare(&o,&e,&ram,5,ram_bytes+16384,201+i,&row)&&!row.bytes);CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(copy_finish(&o,&e,&row));CHECK(!memcmp(&e,&saved,sizeof e));}
 CHECK(o.count==5&&o.used==16732);
 /* Real unchanged engine decode frees space; no manual queue adjustment. */
 CHECK(amc_engine_write(&e,&ram,0xb04,4,0x400)==AMC_ENGINE_OK);CHECK(amc_engine_write(&e,&ram,4,4,1)==AMC_ENGINE_OK);e.next_output=1;
 CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_PUBLISHED);CHECK(e.queue_used==61440&&e.decoded_bytes==4444&&e.published_frames==2);
 uint8_t capture[16384];memcpy(capture,ram_bytes+16384,sizeof capture);capture[348]^=1;saved=e;CHECK(!copy_prepare(&o,&e,&ram,5,capture,230,&row));CHECK(!memcmp(&e,&saved,sizeof e));
 unsigned prior=o.count;o.count=1024;CHECK(!copy_prepare(&o,&e,&ram,5,ram_bytes+16384,230,&row));o.count=prior;
 unsigned used=o.used;o.used=81920;CHECK(!copy_prepare(&o,&e,&ram,5,ram_bytes+16384,230,&row));o.used=used;
 CHECK(copy_prepare(&o,&e,&ram,5,ram_bytes+16384,230,&row)&&row.bytes==4096);CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(copy_finish(&o,&e,&row));CHECK(e.copied_words==1111&&e.queue_used==65536&&e.dma_active&&!e.input_pending);
 CHECK(o.count==6&&o.used==20828);CHECK(!memcmp(o.bytes,ram_bytes,20828));
 /* Standalone lifecycle check may publish extra synthetic frames; the guest
  * wrapper still stops before its third publication work. */
 while(e.dma_active){
  CHECK(amc_engine_sram_write(&e,AMC_HEADER+8+2*e.next_output,2,1)==AMC_ENGINE_OK);
  CHECK(amc_engine_write(&e,&ram,0xb04,4,0x400)==AMC_ENGINE_OK);CHECK(amc_engine_write(&e,&ram,4,4,1)==AMC_ENGINE_OK);
  CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_PUBLISHED);
  CHECK(copy_prepare(&o,&e,&ram,5,ram_bytes+16384,240+o.count,&row)&&row.bytes);
  CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(copy_finish(&o,&e,&row));
 }
 CHECK(e.copied_words==4096&&e.copied_bytes==81920&&e.input_pending);CHECK(o.used==32768&&o.count==9);CHECK(!memcmp(o.bytes,ram_bytes,32768));
 CHECK(amc_engine_write(&e,&ram,0x101c,4,1)==AMC_ENGINE_OK&&!e.input_pending);saved=e;
 CHECK(amc_engine_step(&e,&ram)==AMC_ENGINE_WAIT);CHECK(!memcmp(&e,&saved,sizeof e));
 printf("later admission/backpressure/copy contracts: %u PASS; synthetic RAM/codec, no guest\n",checks);
}
