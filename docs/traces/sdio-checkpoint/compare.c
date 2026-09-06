#include "checkpoint.h"
#include <string.h>
/* Private v39 proof comparator, pinned to snap_cpu's 428-byte encoding.
 * Never edits either snapshot or machine. Only the final three CPU diagnostic
 * counters and the independently verified checksum may differ. */
static uint64_t le(const uint8_t*p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static bool layout(const uint8_t*b,size_t n,size_t*counter){
 if(n<48||memcmp(b,"S5LBox SNAPSHOT\0",16)||le(b+16,4)!=39||le(b+20,4)!=40||le(b+24,8)!=n-48||le(b+32,8))return false;
 uint64_t h=1469598103934665603ull;for(size_t i=40;i<n-8;i++){h^=b[i];h*=1099511628211ull;}if(h!=le(b+n-8,8))return false;
 static const char tags[][5]={"GEOM","CPU ","MACH","RAM ","NOR ","STUB","END "};unsigned section=0;bool found=false;size_t p=40;while(p<n-8){if(n-8-p<16)return false;if(section>=7||memcmp(b+p,tags[section++],4))return false;uint64_t l=le(b+p+8,8);if(le(b+p+4,4)||l>n-8-p-16)return false;
 if(!memcmp(b+p,"CPU ",4)){if(found||l!=428)return false;found=true;*counter=p+16+428-24;}p+=16+(size_t)l;
 }return found&&section==7&&p==n-8;
}
bool checkpoint_arch_equal(const uint8_t*a,size_t an,const arm_cpu_t*ac,const uint8_t*b,size_t bn,const arm_cpu_t*bc){
 size_t ao=0,bo=0;if(an!=bn||!layout(a,an,&ao)||!layout(b,bn,&bo)||ao!=bo)return false;
 uint64_t av[]={ac->tlb_hits,ac->tlb_misses,ac->tlb_flushes},bv[]={bc->tlb_hits,bc->tlb_misses,bc->tlb_flushes};
 for(unsigned i=0;i<3;i++)if(le(a+ao+i*8,8)!=av[i]||le(b+bo+i*8,8)!=bv[i])return false;
 return !memcmp(a,b,ao)&&!memcmp(a+ao+24,b+bo+24,an-8-ao-24);
}
