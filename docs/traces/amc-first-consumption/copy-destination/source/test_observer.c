#include "consume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"observer:%u %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 consume_observer s={0},before;uint8_t pcm[CONSUME_BYTES];for(unsigned i=0;i<sizeof pcm;i++)pcm[i]=(uint8_t)(i*13+7);
 CHECK(consume_read(&s,CONSUME_BASE,4,0,true,0)==CONSUME_IGNORE);CHECK(!s.covered);
 consume_begin(&s,pcm);before=s;
 CHECK(consume_read(&s,CONSUME_BASE,4,0,false,0)==CONSUME_IGNORE);CHECK(!memcmp(&s,&before,sizeof s));
 CHECK(consume_read(&s,CONSUME_BASE-4,4,0,true,0)==CONSUME_IGNORE);
 CHECK(consume_read(&s,CONSUME_BASE+CONSUME_BYTES,4,0,true,0)==CONSUME_IGNORE);
 CHECK(consume_read(&s,UINT32_MAX,4,0,true,0)==CONSUME_IGNORE);
 uint32_t v=(uint32_t)pcm[0]<<24|0xabcdefu;
 CHECK(consume_read(&s,CONSUME_BASE-3,4,v,true,0)==CONSUME_ACCEPT);CHECK(s.covered==1&&s.coverage[0]);
 before=s;CHECK(consume_read(&s,CONSUME_BASE,4,0,true,0)==CONSUME_REJECT);CHECK(!memcmp(&s,&before,sizeof s));
 memcpy(&v,pcm,4);CHECK(consume_read(&s,CONSUME_BASE,4,v,true,1)==CONSUME_REJECT);CHECK(!memcmp(&s,&before,sizeof s));
 v=(uint32_t)pcm[CONSUME_BYTES-1]|0xdeadbe00u;
 CHECK(consume_read(&s,CONSUME_BASE+CONSUME_BYTES-1,4,v,true,0)==CONSUME_ACCEPT);CHECK(s.covered==2);
 /* Exhaust each relative edge/width, comparing an independent overlap count. */
 for(unsigned width=1;width<=4;width*=2)for(int delta=-4;delta<5;delta++)for(unsigned side=0;side<2;side++){
  consume_begin(&s,pcm);uint32_t a=CONSUME_BASE+(side?CONSUME_BYTES:0)+delta;v=0;unsigned expected=0;
  for(unsigned i=0;i<width;i++){uint64_t address=(uint64_t)a+i;uint8_t byte=0xa5;
   if(address>=CONSUME_BASE&&address<(uint64_t)CONSUME_BASE+CONSUME_BYTES){byte=pcm[address-CONSUME_BASE];expected++;}v|=(uint32_t)byte<<(8*i);}
  CHECK(consume_read(&s,a,width,v,true,0)==(expected?CONSUME_ACCEPT:CONSUME_IGNORE));CHECK(s.covered==expected);
 }
 consume_begin(&s,pcm);
 for(unsigned i=0;i<CONSUME_BYTES-1;i++)CHECK(consume_read(&s,CONSUME_BASE+i,1,pcm[i],true,0)==CONSUME_ACCEPT);
 CHECK(!consume_complete(&s));CHECK(consume_read(&s,CONSUME_BASE+CONSUME_BYTES-1,1,pcm[CONSUME_BYTES-1],true,0)==CONSUME_ACCEPT);CHECK(consume_complete(&s));
 before=s;s.reads=CONSUME_RECORD_CAP;consume_observer capped=s;CHECK(consume_read(&s,CONSUME_BASE,1,pcm[0],true,0)==CONSUME_REJECT);CHECK(!memcmp(&s,&capped,sizeof s));s=before;
 before=s;s.coverage[123]=0;CHECK(!consume_complete(&s));s=before;s.seen[123]^=1;CHECK(!consume_complete(&s));
 CHECK(consume_released(0x2200f208,2,1,0,1));CHECK(!consume_released(0x2200f208,2,1,1,1));CHECK(!consume_released(0x2200f20a,2,1,0,1));CHECK(!consume_released(0x2200f208,4,1,0,1));
 printf("consume observer: %u checks passed; synthetic wrapper bytes only\n",checks);return 0;
}
