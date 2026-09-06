#include "retirement.h"
#include <assert.h>
#include <stdio.h>
static unsigned n;
#define CHECK(x) do{n++;assert(x);}while(0)
int main(void){guest_step s={100,0xc02726a8,0x20000013,true,false};
 CHECK(release_start_valid(&s,101,s.pc,s.cpsr,0xe1c030b0));
 CHECK(!release_start_valid(&s,100,s.pc,s.cpsr,0xe1c030b0));CHECK(!release_start_valid(&s,102,s.pc,s.cpsr,0xe1c030b0));CHECK(!release_start_valid(&s,101,s.pc+4,s.cpsr,0xe1c030b0));CHECK(!release_start_valid(&s,101,s.pc,s.cpsr^0x20,0xe1c030b0));CHECK(!release_start_valid(&s,101,s.pc,s.cpsr,0xe1c030b1));
 CHECK(!release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,101,s.pc+4,s.cpsr,false));s.active=false;s.returned=true;
 CHECK(release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,101,s.pc+4,s.cpsr,false));
 for(unsigned delta=0;delta<9;delta++)if(delta!=4)CHECK(!release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,101,s.pc+delta,s.cpsr,false));
 for(unsigned bit=0;bit<32;bit++)CHECK(!release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,101,s.pc+4,s.cpsr^(1u<<bit),false));
 CHECK(!release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,100,s.pc+4,s.cpsr,false));CHECK(!release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,102,s.pc+4,s.cpsr,false));CHECK(!release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,101,s.pc+4,s.cpsr,true));CHECK(!release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,101,0xffff0018,0x92,false));
 s.returned=false;CHECK(!release_finish_valid(&s,101,s.pc,s.cpsr,0xe1c030b0,101,s.pc+4,s.cpsr,false));
 s=(guest_step){100,0xc0272690,0x60000013,true,false};CHECK(release_start_valid(&s,101,s.pc,s.cpsr,0x01c120b0));s.cpsr^=1u<<30;CHECK(!release_start_valid(&s,101,s.pc,s.cpsr,0x01c120b0));
 printf("actual instruction-start/return retirement: %u PASS; synthetic only\n",n);}
