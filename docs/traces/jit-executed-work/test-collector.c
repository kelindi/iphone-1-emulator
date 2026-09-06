#include "census.h"
#include <assert.h>
#include <stdio.h>
static const fc_row *last(unsigned admission,unsigned reason,unsigned flags) {
 const fc_row *rs=fc_rows(); for(unsigned n=0;n<fc_capacity();n++)
 if(rs[n].used && rs[n].admission==admission && rs[n].provenance==reason && rs[n].flags==flags)return &rs[n];
 assert(!"expected row absent");return 0;
}
int main(void) {
 uint32_t mask[16]={1}; fc_reset();fc_install(0x1000,0,mask);fc_start();
 fc_attempt(FC_RUNTIME_GUARD,0x1000,0,0x20);fc_begin(0x1000,0);fc_fetch(0xe1a00000);fc_end(0,1);
 assert(last(FC_ADMITTED,FC_RUNTIME_GUARD,FC_FETCH)->guard==0x20);
 fc_begin(0x2000,0);fc_fetch(0xe1a00000);fc_end(0,1);
 assert(last(FC_OUTSIDE_BASE,FC_NONE,FC_FETCH)->count==1);
 fc_begin(0x1000,1);fc_fetch(0x46c0);fc_end(0,1);
 assert(last(FC_OTHER_MODE,FC_NONE,FC_FETCH|0x100)->opcode==0x46c0);
 fc_attempt(FC_GENERATED_PREFIX,0x1004,0,0);fc_begin(0x1004,0);fc_fetch(0xe1a00000);fc_end(0,1);
 assert(last(FC_NO_ADMISSION,FC_GENERATED_PREFIX,FC_FETCH)->count==1);
 fc_stale(0x1000,0);fc_attempt(FC_STALE_ZERO,0x1000,0,0);fc_begin(0x1000,0);fc_fetch(0xe1a01001);fc_end(0,1);
 assert(last(FC_INACTIVE,FC_STALE_ZERO,FC_FETCH)->opcode==0xe1a01001);
 fc_begin(0x3000,0);fc_event(FC_PREFETCH);fc_end(0,1);
 assert(last(FC_OUTSIDE_BASE,FC_NONE,FC_PREFETCH)->opcode==0);
 fc_begin(0x3000,0);fc_event(FC_IRQ);fc_end(0,1);
 assert(last(FC_OUTSIDE_BASE,FC_NONE,FC_IRQ)->count==1);
 fc_begin(0x3000,0);fc_fetch(0xe5910000);fc_event(FC_DATA_ABORT);fc_end(0,1);
 assert(last(FC_OUTSIDE_BASE,FC_NONE,FC_FETCH|FC_DATA_ABORT)->count==1);
 fc_attempt(FC_GENERATED_ZERO,0x1000,0,0);fc_begin(0x4000,0);fc_end(2,0);
 assert(last(FC_OUTSIDE_BASE,FC_MISMATCH,0)->status==2);
 const fc_totals *t=fc_summary();assert(t->calls==9 && t->ok==8 && t->nonok==1 && t->fetched==6 && t->nofetch==3 && t->overflow==0);
 uint64_t sum=0;for(unsigned i=0;i<fc_capacity();i++)sum+=fc_rows()[i].count;assert(sum==t->calls);
 fc_stop();fc_begin(0,0);fc_fetch(0);fc_end(0,1);assert(t->calls==9);
 fc_start();assert(t->calls==0);fc_begin(0x1000,0);fc_fetch(0);fc_end(0,1);assert(last(FC_INACTIVE,FC_NONE,FC_FETCH)->count==1);
 fc_reset();fc_start();fc_begin(0x1000,0);fc_fetch(0);fc_end(0,1);assert(last(FC_OUTSIDE_BASE,FC_NONE,FC_FETCH)->count==1);

 /* Same PC, different fetched opcode must remain two rows. */
 fc_reset();fc_start();fc_begin(0x2000,0);fc_fetch(1);fc_end(0,1);
 fc_begin(0x2000,0);fc_fetch(2);fc_end(0,1);assert(fc_summary()->rows==2);
 /* Every attempt label is retained without inferring an opcode fault. */
 for(unsigned reason=FC_NONE;reason<=FC_STALE_ZERO;reason++) {
   fc_reset();fc_start();fc_attempt(reason,0x2000,1,0);
   fc_begin(0x2000,1);fc_fetch(0x46c0);fc_end(0,1);
   assert(last(FC_OUTSIDE_BASE,reason,FC_FETCH|0x100)->count==1);
 }
 fc_reset();fc_start();fc_attempt(FC_RUNTIME_GUARD,0x2000,0,511);
 fc_attempt(FC_NONE,0x2000,0,0);fc_begin(0x2000,0);fc_event(FC_FIQ);fc_end(0,1);
 assert(last(FC_OUTSIDE_BASE,FC_NONE,FC_FIQ)->guard==0);
 uint64_t epoch=fc_summary()->epoch;fc_install(0x1000,0,mask);
 assert(fc_summary()->installs_during_capture==1);fc_reset();
 assert(fc_summary()->epoch==epoch+1 && !fc_summary()->enabled);
 /* Reset/start never conceals a full admission-map failure. */
 for(unsigned i=0;i<2049;i++)fc_install(i*1024,0,mask);
 assert(fc_summary()->map_overflow==1);fc_start();assert(fc_summary()->map_overflow==1);
 puts("collector focused contracts PASS");
}
