#include "checkpoint.h"
#include <stdlib.h>
#include <string.h>
#define BUNDLE_CAP 300000u
#define Z1_BYTES 1490u
static const uint8_t magic[8]={'S','D','I','O','Z','1',0,0};
typedef struct {uint8_t*out;const uint8_t*in;size_t pos,n;bool read,ok;} zstream;
static uint64_t number(zstream*w,uint64_t v,unsigned n){if(!w->ok||w->pos>w->n||n>w->n-w->pos){w->ok=false;return 0;}uint64_t r=0;for(unsigned i=0;i<n;i++){if(w->read)r|=(uint64_t)w->in[w->pos+i]<<(8*i);else w->out[w->pos+i]=(uint8_t)(v>>(8*i));}w->pos+=n;return w->read?r:v;}
static void bytes(zstream*w,uint8_t*p,size_t n){if(!w->ok||w->pos>w->n||n>w->n-w->pos){w->ok=false;return;}if(w->read)memcpy(p,w->in+w->pos,n);else memcpy(w->out+w->pos,p,n);w->pos+=n;}
static uint64_t checksum(const uint8_t*b,size_t n){uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ull;}return h;}
static void fields(zstream*w,s5l_mtz1_t*s){
#define BOOL(p) do{uint64_t v=number(w,s->p?1:0,1);if(v>1)w->ok=false;s->p=v!=0;}while(0);
#define U8(p) s->p=(uint8_t)number(w,s->p,1);
#define U16(p) s->p=(uint16_t)number(w,s->p,2);
#define U32(p) s->p=(uint32_t)number(w,s->p,4);
#define U64(p) s->p=number(w,s->p,8);
#define BYTES(p) bytes(w,s->p,sizeof s->p);
#include "z1-fields.inc"
#undef BOOL
#undef U8
#undef U16
#undef U32
#undef U64
#undef BYTES
}
static bool contacts(const s5l_mt_contact_t*c,unsigned n){unsigned ids=0;for(unsigned i=0;i<n;i++){if(!c[i].id||c[i].id>MTZ2_CONTACT_MAX||c[i].phase>MTZ2_PHASE_OUT_OF_RANGE||c[i].x>=S5L_MT_PANEL_W||c[i].y>=S5L_MT_PANEL_H||(ids&(1u<<c[i].id)))return false;ids|=1u<<c[i].id;}return true;}
static bool valid(const s5l_mtz1_t*z){
 if(z->pending_len>sizeof z->pending||z->last_n>MTZ2_CONTACT_MAX||z->held_n>MTZ2_CONTACT_MAX||z->uptime_tick_acc>=MTZ1_TICKS_PER_MS||z->tick_acc>=MTZ1_FRAME_TICKS)return false;
 /* Long streamed transfers can exceed the finite retained prefix. Their count
  * remains representable only with the actual overflow evidence. Do not cap
  * an ordinary raw stream at1024 or invent missing bytes. Wrapped counts are
  * outside private checkpoint admission. */
 if(z->pkt_len>z->rx_len||(z->transaction_overflow!=(z->rx_len>sizeof z->rx)))return false;
 return contacts(z->last_contacts,z->last_n)&&contacts(z->held,z->held_n);
}
bool checkpoint_bundle_save(const sdio_t*s,const s5l_mtz1_t*z,const checkpoint_instant_t*i,uint8_t**out,size_t*n){
 if(!s||!z||!i||!out||!n||!valid(z))return false;uint8_t*sb=NULL;size_t sn=0;
 if(!checkpoint_side_save(s,i,&sb,&sn))return false;
 if(sn>BUNDLE_CAP-24-Z1_BYTES-8){free(sb);return false;}size_t total=24+sn+Z1_BYTES+8;uint8_t*b=malloc(total);if(!b){free(sb);return false;}
 zstream w={.out=b,.n=total,.ok=true};uint8_t m[8];memcpy(m,magic,8);bytes(&w,m,8);number(&w,2,4);number(&w,total,4);number(&w,sn,4);number(&w,Z1_BYTES,4);bytes(&w,sb,sn);free(sb);
 s5l_mtz1_t copy=*z;fields(&w,&copy);uint64_t h=checksum(b,w.pos);number(&w,h,8);if(!w.ok||w.pos!=total){free(b);return false;}*out=b;*n=total;return true;
}
bool checkpoint_bundle_load(sdio_t*s,s5l_mtz1_t*z,checkpoint_instant_t*i,const uint8_t*b,size_t n){
 if(!s||!z||!i||!b||n<32||n>BUNDLE_CAP)return false;
 zstream w={.in=b,.n=n,.read=true,.ok=true};uint8_t m[8];bytes(&w,m,8);uint64_t version=number(&w,0,4),total=number(&w,0,4),sn=number(&w,0,4),zn=number(&w,0,4);
 if(!w.ok||memcmp(m,magic,8)||version!=2||total!=n||zn!=Z1_BYTES||sn>n-32||zn!=n-32-sn)return false;
 zstream trailer={.in=b+n-8,.n=8,.read=true,.ok=true};if(number(&trailer,0,8)!=checksum(b,n-8))return false;
 /* Preserve already loaded native contact padding in host memory only. The
  * portable record still contains explicit fields exclusively. */
 sdio_t*sd=calloc(1,sizeof *sd);if(!sd)return false;s5l_mtz1_t zd;memcpy(&zd,z,sizeof zd);checkpoint_instant_t at={0};
 bool ok=checkpoint_side_load(sd,&at,b+24,(size_t)sn);w.pos+=sn;fields(&w,&zd);ok=ok&&w.ok&&w.pos==n-8&&valid(&zd);
 if(ok){sd->dma_reader=s->dma_reader;sd->dma_ctx=s->dma_ctx;zd.trace=z->trace;*s=*sd;memcpy(z,&zd,sizeof *z);*i=at;}free(sd);return ok;
}
