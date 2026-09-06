#include "census.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void){
 fc_reset();assert(fc_generated_capacity()==512u*512u);assert(!*fc_enabled_pointer());
 uint64_t *g=fc_generated_counts(),*o=fc_generated_overflow();assert((uintptr_t)g%8==0&&(uintptr_t)o%8==0);
 g[0]=7;g[512u*512u-1]=9;*o=1;fc_start();assert(*fc_enabled_pointer());assert(!g[0]&&!g[512u*512u-1]&&!*o);
 g[0]=13;g[512u*512u-1]=21;*o=2;fc_stop();assert(!*fc_enabled_pointer());assert(g[0]==13&&g[512u*512u-1]==21&&*o==2);
 fc_reset();assert(!g[0]&&!g[512u*512u-1]&&!*o&&!*fc_enabled_pointer());
 puts("Generated observer storage lifecycle PASS (actual emitter counting tested separately)");
}
