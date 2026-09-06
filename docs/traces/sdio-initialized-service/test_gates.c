#include "gates.h"
#include <stdio.h>
static unsigned checks,failed;
#define CHECK(x) do{checks++;if(!(x)){failed++;fprintf(stderr,"FAIL line%u %s\n",__LINE__,#x);}}while(0)
int main(void){
 for(unsigned flags=0;flags<256;flags++)for(unsigned low=0;low<64;low++)CHECK(gate_arm_svc((flags<<24)|low)==(low==0x13));
 gate_pair_t p={0};gate_pair_read(&p,0x35,0x10fe,true,5);CHECK(!p.complete);
 gate_pair_read(&p,0x34,0x10dc,true,8);CHECK(p.have_low&&!p.complete);gate_pair_read(&p,0x35,0x10fe,true,9);CHECK(p.complete);
 gate_pair_begin(&p);CHECK(!p.complete); /* stale stack FEDC on next iteration */
 for(unsigned kind=0;kind<8;kind++){gate_pair_begin(&p);gate_pair_read(&p,0x34,0x10dc,true,10);
  if(kind==0)gate_pair_read(&p,0x35,0x10fe,false,11);
  if(kind==1)gate_pair_read(&p,0x35,0x1000,true,11);
  if(kind==2)gate_pair_read(&p,0x20,0x1009,true,11);
  if(kind==3)gate_pair_read(&p,0x35,0x10fe,true,12);
  if(kind==4){gate_pair_begin(&p);gate_pair_read(&p,0x35,0x10fe,true,11);}
  if(kind==5){gate_pair_read(&p,0x34,0x1000,true,11);gate_pair_read(&p,0x35,0x10fe,true,12);}
  if(kind==6){gate_pair_read(&p,0x34,0x10dc,false,11);gate_pair_read(&p,0x35,0x10fe,true,12);}
  if(kind==7){gate_pair_read(&p,0x34,0x10dc,true,UINT64_MAX);gate_pair_read(&p,0x35,0x10fe,true,0);}
  CHECK(!p.complete);
 }
 printf("%u gate checks, %u failures\n",checks,failed);return failed?1:0;
}
