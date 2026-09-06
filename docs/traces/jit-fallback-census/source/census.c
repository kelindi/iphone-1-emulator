#include "census.h"
#include <string.h>
#ifndef FC_CAPACITY
#define FC_CAPACITY 524288u
#endif
#define FC_MAP 2048u
_Static_assert((FC_CAPACITY & (FC_CAPACITY-1)) == 0, "power of two");
typedef struct { uint32_t base, thumb, mask[16], used, active; } seed;
static seed seeds[FC_MAP];
static fc_row rows[FC_CAPACITY], current;
static fc_totals total;
static uint64_t reset_epoch;
static uint32_t pending_pc, pending_thumb, pending_reason, pending_guard;
static seed *lookup(uint32_t pc, unsigned thumb, unsigned create) {
    uint32_t base=pc&~1023u, slot=((base>>10)^(base>>18)^(thumb*0x987u))&(FC_MAP-1);
    for (unsigned n=0;n<FC_MAP;n++) {
        seed *s=&seeds[(slot+n)&(FC_MAP-1)];
        if (!s->used) { if (!create) return 0; s->used=1; s->base=base; s->thumb=thumb; return s; }
        if (s->base==base && s->thumb==thumb) return s;
    }
    if (create) total.map_overflow++;
    return 0;
}
void fc_reset(void) { memset(seeds,0,sizeof seeds); memset(rows,0,sizeof rows); memset(&total,0,sizeof total); pending_reason=0; pending_guard=0; total.epoch=++reset_epoch; }
void fc_start(void) { uint64_t map_overflow=total.map_overflow; memset(rows,0,sizeof rows); memset(&total,0,sizeof total); total.map_overflow=map_overflow; total.epoch=reset_epoch; total.enabled=1; pending_reason=0; pending_guard=0; pending_guard=0; }
void fc_stop(void) { total.enabled=0; pending_reason=0; pending_guard=0; }
void fc_install(uint32_t pc,unsigned thumb,const uint32_t *mask) { total.installs_during_capture+=!!total.enabled; seed *s=lookup(pc,thumb,1); if(s) { memcpy(s->mask,mask,sizeof s->mask); s->active=1; } }
void fc_stale(uint32_t pc,unsigned thumb) { seed *s=lookup(pc,thumb,0); if(s) s->active=0; }
void fc_attempt(unsigned reason,uint32_t pc,unsigned thumb,unsigned guard) { if(!total.enabled)return; pending_reason=reason;pending_pc=pc;pending_thumb=thumb;pending_guard=guard; }
void fc_begin(uint32_t pc,unsigned thumb) {
    if(!total.enabled)return;
    memset(&current,0,sizeof current); current.pc=pc;current.flags=thumb?0x100u:0;
    current.provenance=pending_reason;
    if(pending_reason && (pending_pc!=pc || pending_thumb!=thumb)) current.provenance=FC_MISMATCH;
    current.guard=pending_guard;pending_reason=0;pending_guard=0;
    seed *s=lookup(pc,thumb,0);
    if(!s) current.admission=lookup(pc,!thumb,0)?FC_OTHER_MODE:FC_OUTSIDE_BASE;
    else { unsigned bit=(pc&1023u)>>(thumb?1:2);current.admission=!(s->mask[bit>>5]&(1u<<(bit&31)))?FC_NO_ADMISSION:!s->active?FC_INACTIVE:FC_ADMITTED; }
}
void fc_event(unsigned flag) { if(total.enabled)current.flags|=flag; }
void fc_fetch(uint32_t opcode) { if(total.enabled) {current.opcode=opcode;current.flags|=FC_FETCH;} }
void fc_end(unsigned status,unsigned ok) {
    if(!total.enabled)return;
    total.calls++;total.ok+=!!ok;total.nonok+=!ok;
    total.fetched+=!!(current.flags&FC_FETCH);total.nofetch+=!(current.flags&FC_FETCH);
    current.status=status;current.used=1;
    uint32_t h=2166136261u; const uint32_t words[8]={current.pc,current.opcode,current.flags,current.provenance,current.guard,current.admission,current.status,current.used};
    for(unsigned i=0;i<8;i++)h=(h^words[i])*16777619u;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    for(unsigned n=0;n<64u && n<FC_CAPACITY;n++) {
        fc_row *r=&rows[(h+n*((h>>16)|1u))&(FC_CAPACITY-1)];
        if(!r->used) {*r=current;r->count=1;total.rows++;return;}
        if(!memcmp(r,&current,8*sizeof(uint32_t))) {r->count++;return;}
    }
    total.overflow++;
}
const fc_totals *fc_summary(void){return &total;}
const fc_row *fc_rows(void){return rows;}
unsigned fc_capacity(void){return FC_CAPACITY;}
