#include "census.h"
#include <assert.h>
#include <stdio.h>
int main(void){fc_reset();fc_start();uint64_t epoch=fc_summary()->epoch;fc_attempt(FC_GENERATED_PREFIX,0x2000,0,0);*fc_enabled_pointer()=0;fc_begin(0x9990,1);fc_fetch(0xffff);fc_end(0,1);assert(fc_summary()->calls==0);*fc_enabled_pointer()=1;fc_begin(0x2000,0);fc_fetch(0xe2800001);fc_end(0,1);assert(fc_summary()->calls==1&&fc_summary()->epoch==epoch);unsigned found=0;for(unsigned i=0;i<fc_capacity();i++){const fc_row*r=fc_rows()+i;if(r->used){assert(r->pc==0x2000&&r->opcode==0xe2800001&&r->provenance==FC_GENERATED_PREFIX&&r->count==1);found++;}}assert(found==1);puts("Paused enabled bit preserves pending provenance and cumulative count PASS");}
