#include "amc_idle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%u %s\n",__LINE__,#x);exit(1);}}while(0)
static const uint32_t beforeO[]={0xa00,0x184,0x180,0xb10,0xb04,0x340};
static const uint32_t beforeV[]={0,0,1,UINT32_MAX,UINT32_MAX,0};
static const uint32_t afterO[]={0x101c,0x100c,0x1000,0x1004,0x1008,0xa00,0x300,0x310,0x304,0x314,0x308,0x318,0x30c,0x31c,0x320,0x324,0x300,0x304,0xd0c};
static const uint32_t afterV[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x07000001,0x3f081603,0x40};
static void invariant(amc_idle_t*s){uint32_t v;CHECK(amc_idle_read(s,0xb18,4,&v)==AMC_ACCEPT);CHECK(!(v&0x200));}
static void setup(amc_idle_t*s){amc_idle_init(s);amc_idle_dependencies(s,0,0);CHECK(amc_idle_reset(s,0xa000)==AMC_ACCEPT);CHECK(amc_idle_reset(s,0xa00)==AMC_ACCEPT);for(unsigned i=0;i<6;i++)CHECK(amc_idle_write(s,beforeO[i],4,beforeV[i])==AMC_ACCEPT);}
static void reject(amc_idle_t*s,uint32_t o,unsigned w,uint32_t v){amc_idle_t old=*s;CHECK(amc_idle_write(s,o,w,v)==AMC_REJECT);CHECK(!memcmp(&old,s,sizeof old));invariant(s);}
int main(void){
 amc_idle_t s,old;amc_idle_init(&s);old=s;CHECK(!amc_idle_observe_upload(&s));CHECK(!memcmp(&old,&s,sizeof s));
 setup(&s);reject(&s,0x101c,4,1);CHECK(amc_idle_observe_upload(&s));old=s;CHECK(!amc_idle_observe_upload(&s));CHECK(!memcmp(&old,&s,sizeof s));
 uint32_t v;CHECK(amc_idle_read(&s,0x101c,4,&v)==AMC_ACCEPT&&v==0);
 for(unsigned i=0;i<19;i++){
  reject(&s,afterO[i],4,afterV[i]^2);reject(&s,afterO[i],2,afterV[i]);reject(&s,0,4,1);
  CHECK(amc_idle_write(&s,afterO[i],4,afterV[i])==AMC_ACCEPT);CHECK(!s.input_pending);invariant(&s);
  if(i==0)reject(&s,0x101c,4,1); /* Duplicate ack is rejected, never becomes an event. */
 }
 old=s;CHECK(amc_idle_write(&s,0,4,1)==AMC_RUN_REQUEST);CHECK(!memcmp(&old,&s,sizeof s));invariant(&s);
 reject(&s,4,4,1);reject(&s,8,4,1);reject(&s,0xc,4,1);reject(&s,0x1018,4,1);reject(&s,0x100c,4,1);
 CHECK(amc_idle_reset(&s,0xa00)==AMC_ACCEPT);CHECK(!s.upload_observed&&!s.post_prefix&&!s.input_pending);
 setup(&s);CHECK(amc_idle_observe_upload(&s));
 s.input_pending=true; /* Native unit-only pending latch; no guest event producer. */
 CHECK(amc_idle_read(&s,0x101c,4,&v)==AMC_ACCEPT&&v==1);CHECK(amc_idle_write(&s,0x101c,4,1)==AMC_ACCEPT);CHECK(amc_idle_read(&s,0x101c,4,&v)==AMC_ACCEPT&&v==0);
 for(unsigned clock=0;clock<3;clock++){
  setup(&s);CHECK(amc_idle_observe_upload(&s));amc_idle_dependencies(&s,clock==0?8:clock==1?16:0,clock==2?16:0);
  CHECK(!s.upload_observed);reject(&s,0x101c,4,1);amc_idle_dependencies(&s,0,0);reject(&s,0x101c,4,1);
 }
 printf("isolated post-upload idle contract: %u checks passed\n",checks);return 0;
}
