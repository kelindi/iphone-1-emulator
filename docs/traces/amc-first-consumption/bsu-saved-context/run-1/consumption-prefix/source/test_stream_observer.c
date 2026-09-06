#include "stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static stream_observer o,save;static amc_engine e;static amc_sram_t ram;static unsigned checks;
#define CHECK(v) do{checks++;assert(v);}while(0)
static void half(unsigned a,unsigned v){ram.bytes[a]=v;ram.bytes[a+1]=v>>8;}
int main(void){
 e.sram=&ram;e.configured=true;e.service=true;e.queue_used=50;
 half(AMC_HEADER+8,1);half(AMC_HEADER+10,1);
 for(unsigned n=0;n<64;n++){
  unsigned slot=n%2,base=slot?AMC_OUTPUT1:AMC_OUTPUT0;uint32_t address=0x22000000+base;
  CHECK(stream_prepare(&o,&e));e.published_frames++;e.next_output=slot^1;half(AMC_HEADER+8+2*slot,0);half(AMC_HEADER+12+2*slot,2048);memset(ram.bytes+base,n+1,4096);
  CHECK(stream_publish(&o,&e,slot,10000*n+1));save=o;CHECK(!stream_publish(&o,&e,slot,10000*n+1));CHECK(!memcmp(&save,&o,sizeof o));
  CHECK(stream_ipc(&o,slot,10000*n+2,false));CHECK(stream_ipc(&o,slot,10000*n+3,true));stream_ack(&o,10000*n+4);
  guest_step step={.cycles=10000*n+5000,.pc=0xc02726a8,.cpsr=0x20000013,.active=true};
  CHECK(!stream_release_begin(&o,&e,0x2200f208+2*slot,2,1,&step,step.cycles+1,step.pc,step.cpsr,0xe1c030b0,0xe1230000,slot,true));
  save=o;CHECK(!stream_read(&o,&e,address,4,0,0x3009a20c,0x60000010,10000*n+5));CHECK(!memcmp(&save,&o,sizeof o));
  for(unsigned j=0;j<4096;j+=4){uint32_t v=(n+1)*0x01010101u;CHECK(stream_read(&o,&e,address+j,4,v,0x3009a20c,0x60000010,10000*n+5+j));}
  CHECK(consume_complete(&o.gen[n].reads));
  CHECK(!stream_release_begin(&o,&e,0x2200f208+2*slot,2,1,&step,step.cycles+1,step.pc,step.cpsr^32,0xe1c030b0,0xe1230000,slot,true));
  CHECK(stream_release_begin(&o,&e,0x2200f208+2*slot,2,1,&step,step.cycles+1,step.pc,step.cpsr,0xe1c030b0,0xe1230000,slot,true));
  half(AMC_HEADER+8+2*slot,1);step.active=false;step.returned=true;
  CHECK(!stream_release_finish(&o,&e,&step,step.cycles+1,step.pc+8,step.cpsr,false));
  CHECK(!stream_release_finish(&o,&e,&step,step.cycles+1,step.pc+4,step.cpsr,true));
  CHECK(stream_release_finish(&o,&e,&step,step.cycles+1,step.pc+4,step.cpsr,false));
  CHECK(!stream_read(&o,&e,address,4,(n+1)*0x01010101u,0x3009a20c,0x60000010,step.cycles+2));
 }
 /* Declared free hardware owner cannot hide missing observed release. */
 e.next_output=0;o.gen[62].released=false;CHECK(!stream_prepare(&o,&e));o.gen[62].released=true;
 CHECK(o.count==64&&o.release_count==64&&o.read_count==65536);CHECK(!stream_prepare(&o,&e));
 e.events=0x400;CHECK(stream_prepare(&o,&e));e.events=0;e.service=false;CHECK(stream_prepare(&o,&e));
 amc_physical_ram physical={.base=0x10000000,.size=0x10000000};e.initialized=true;e.configured=true;e.input_phase=5;e.transport_enabled=true;e.staged_words=e.dma_words=e.copied_words=4096;e.copied_bytes=5*16384;e.decoded_bytes=20000;e.queue_used=e.copied_bytes-e.decoded_bytes;e.staged_address=0x14000000;
 CHECK(stream_admit(&e,&physical,6,0xc0278130,0xc0274874,0x13));CHECK(!stream_admit(&e,&physical,6,0xc0278130,0xc0274874,0x33));e.dma_active=true;CHECK(!stream_admit(&e,&physical,6,0xc0278130,0xc0274874,0x13));
 /* Partial unaligned overlap records only returned PCM bytes, for slot1 too. */
 memset(&o,0,sizeof o);e.published_frames=1;e.next_output=0;half(AMC_HEADER+10,0);half(AMC_HEADER+14,2048);memset(ram.bytes+AMC_OUTPUT1,0x12,4096);
 CHECK(stream_publish(&o,&e,1,1));CHECK(stream_read(&o,&e,0x2201121f,4,0x12121299,0x3009a20c,0x60000010,2));CHECK(o.gen[0].reads.covered==3);
 save=o;CHECK(!stream_read(&o,&e,0x2201121f,4,0x13121299,0x3009a20c,0x60000010,3));CHECK(!memcmp(&save,&o,sizeof o));
 printf("PASS %u generation/read/release/admission checks; synthetic observer only\n",checks);return 0;
}
