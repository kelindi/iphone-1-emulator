#define main retained_lock_main
#include "test_lock.c"
#undef main
#include "bsu_save.h"
static bsu_save saved,oldsave;static bool boundary=true;static amc_codec_ops saveops;
static bool at_boundary(const void*p){return p==&history&&boundary;}
static void setup(void){init();memset(&saved,0,sizeof saved);saveops=codec;saveops.at_frame_boundary=at_boundary;e.codec=&saveops;e.generation=1;e.bsu_locked=e.dma_paused=true;e.transport_enabled=false;boundary=true;}
static void bad(unsigned off,unsigned width){oldsave=saved;prior=e;uint32_t out=0xdead1234;CHECK(!bsu_save_read(&saved,&e,off,width,&out));CHECK(out==0xdead1234&&!memcmp(&saved,&oldsave,sizeof saved)&&!memcmp(&e,&prior,sizeof e));}
int main(void){
 setup();bad(0x414,4);bad(0x188,4);bad(0x404,4);bad(0x410,2);bad(0x47c,4);
 #define FAIL(field,value) do{setup();e.field=value;bad(0x410,4);}while(0)
 FAIL(bsu_locked,false);FAIL(dma_paused,false);FAIL(transport_enabled,true);FAIL(generation,0);FAIL(generation,UINT64_MAX);FAIL(decoded_frames,0);FAIL(queue_used,65537);FAIL(copied_bytes,0);FAIL(fault,true);FAIL(decoder,NULL);FAIL(dma_words,0);FAIL(copied_words,4096);
 setup();boundary=false;bad(0x410,4);setup();sram.bytes[AMC_HEADER+8]=2;bad(0x410,4);
 setup();uint32_t out;CHECK(bsu_save_read(&saved,&e,0x410,4,&out)&&out==1);CHECK(saved.owned&&saved.phase==1&&bsu_save_valid(&saved,&e));bad(0x410,4);bad(0x404,4);bad(0x188,4);
 for(unsigned i=1;i<7;i++)CHECK(bsu_save_read(&saved,&e,0x410+4*i,4,&out)&&out==saved.words[i]);CHECK(saved.phase==7);bad(0x404,4);CHECK(bsu_save_read(&saved,&e,0x188,4,&out)&&out==0);bad(0x47c,4);CHECK(bsu_save_read(&saved,&e,0x404,4,&out)&&out==0&&saved.phase==9);bad(0x404,4);bad(0x410,4);
 CHECK(amc_engine_dependencies(&e,0x18,0)==AMC_ENGINE_REJECT);reject(0xc,4,1);reject(0x400,4,0);reject(0x410,4,1);
 for(unsigned i=0;i<7;i++){setup();CHECK(bsu_save_read(&saved,&e,0x410,4,&out));switch(i){case 0:e.generation++;break;case 1:e.queue_epoch++;break;case 2:q[0]^=1;break;case 3:sram.bytes[5]^=1;break;case 4:idle.divider++;break;case 5:boundary=false;break;case 6:e.events^=0x400;break;}bad(0x414,4);}
 printf("PASS %u transactional save/resource/order/immutability checks; synthetic resources only\n",checks);
}
