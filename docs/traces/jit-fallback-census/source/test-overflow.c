#include "census.h"
#include <assert.h>
int main(void){ fc_reset();fc_start();for(unsigned i=0;i<100;i++){fc_begin(i*4,0);fc_fetch(i);fc_end(0,1);}const fc_totals*t=fc_summary();uint64_t sum=0;for(unsigned i=0;i<fc_capacity();i++)sum+=fc_rows()[i].count;assert(t->calls==100);assert(t->overflow>0);assert(sum+t->overflow==100); }
