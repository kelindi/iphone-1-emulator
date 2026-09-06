#include "census.h"
#include <assert.h>
#include <stdio.h>
int main(void){fc_reset();fc_start();for(unsigned i=0;i<386000;i++){fc_begin(i*4u*0x9e3779b1u,i&1u);fc_attempt(FC_NONE,0,0,0);fc_fetch(i^0xe1a00000u);fc_end(0,1);}const fc_totals*t=fc_summary();uint64_t sum=0;for(unsigned i=0;i<fc_capacity();i++)sum+=fc_rows()[i].count;assert(t->calls==386000&&t->rows==386000&&t->overflow==0&&sum==t->calls);printf("386000 unique synthetic rows, zero overflow, %u capacity\n",fc_capacity());}
