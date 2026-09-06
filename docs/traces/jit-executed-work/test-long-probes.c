#include "census.h"
#include <assert.h>
#include <stdio.h>
static uint32_t hash(uint32_t pc){uint32_t h=2166136261u,w[8]={pc,0xe1a00000,FC_FETCH,FC_NONE,0,FC_OUTSIDE_BASE,0,1};for(unsigned i=0;i<8;i++)h=(h^w[i])*16777619u;h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;return h;}
int main(void){fc_reset();fc_start();unsigned found=0;for(unsigned i=0;i<2000000&&found<65;i++){uint32_t pc=i*4,h=hash(pc);if((h&127)!=0||(((h>>16)|1)&127)!=1)continue;fc_begin(pc,0);fc_fetch(0xe1a00000);fc_end(0,1);found++;}const fc_totals*t=fc_summary();assert(found==65&&t->calls==65&&t->rows==64&&t->overflow==1);uint64_t sum=0;for(unsigned i=0;i<fc_capacity();i++)sum+=fc_rows()[i].count;assert(sum+t->overflow==65);puts("65 identical slot/stride keys: 64 admitted, explicit overflow, conservation PASS");}
