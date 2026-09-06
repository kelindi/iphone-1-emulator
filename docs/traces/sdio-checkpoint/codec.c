#include "checkpoint.h"
#include <stdlib.h>
#include <string.h>
/* Canonical little-endian, explicit fields. No native padding or pointers. */
#define SIDE_CAP 300000u
static const uint8_t magic[8]={'S','D','I','O','C','K','P',0};
typedef struct { uint8_t *out; const uint8_t *in; size_t pos,size; bool read,ok; } stream_t;
static uint64_t number(stream_t *w,uint64_t v,unsigned n) {
 if(!w->ok||w->pos>w->size||n>w->size-w->pos){w->ok=false;return 0;}
 uint64_t x=0;for(unsigned i=0;i<n;i++){if(w->read)x|=(uint64_t)w->in[w->pos+i]<<(8*i);else w->out[w->pos+i]=(uint8_t)(v>>(8*i));}
 w->pos+=n;return w->read?x:v;
}
static void bytes(stream_t *w,uint8_t *p,size_t n) {
 if(!w->ok||w->pos>w->size||n>w->size-w->pos){w->ok=false;return;}
 if(w->read)memcpy(p,w->in+w->pos,n);else memcpy(w->out+w->pos,p,n);w->pos+=n;
}
static void instant(stream_t*w,checkpoint_instant_t*i) {
 i->ticks=number(w,i->ticks,8);i->cycles=number(w,i->cycles,8);
 i->pc=(uint32_t)number(w,i->pc,4);i->cpu_hz=(uint32_t)number(w,i->cpu_hz,4);
 i->ram_base=(uint32_t)number(w,i->ram_base,4);i->ram_size=(uint32_t)number(w,i->ram_size,4);
}
static void fields(stream_t*w,sdio_t*s) {
#define U8(p) s->p=(uint8_t)number(w,s->p,1);
#define U16(p) s->p=(uint16_t)number(w,s->p,2);
#define U32(p) s->p=(uint32_t)number(w,s->p,4);
#define U64(p) s->p=number(w,s->p,8);
#define ENUM(p) s->p=(unsigned)number(w,s->p,4);
#define BOOL(p) do {uint64_t x=number(w,s->p?1:0,1);if(x>1)w->ok=false;s->p=x!=0;}while(0);
#define BYTES(p) bytes(w,s->p,sizeof s->p);
#include "fields.inc"
#undef U8
#undef U16
#undef U32
#undef U64
#undef ENUM
#undef BOOL
#undef BYTES
}
static bool valid(const sdio_t*s,const checkpoint_instant_t*i) {
 sdio_t *probe=malloc(sizeof *probe);if(!probe)return false;
 bool config=sdio_init(probe,&s->config);free(probe);if(!config)return false;
 if(i->ram_base!=0x08000000u||!i->ram_size||i->ram_size>(256u<<20)||i->ram_base>UINT32_MAX-i->ram_size||!i->cpu_hz)return false;
 if(s->now!=i->ticks||(!s->have_time&&s->now))return false;
 if(s->card.state>CARD_SELECTED||s->main_model.phase>MAIN_HANDOFF)return false;
 if(s->endpoint.capacity>SDIO_ENDPOINT_CAPACITY||s->endpoint.used>s->endpoint.capacity||
    s->helper.used>s->config.helper_capacity||s->helper.used>SDIO_HELPER_CAPACITY||
    s->data.length>sizeof s->data.staging||s->main_model.requested>512||s->main_model.pending_length>512)return false;
 if(s->endpoint.initialized&&(s->endpoint.capacity!=s->config.endpoint_capacity||!s->endpoint.enabled))return false;
 if(s->endpoint.ready&&(!s->endpoint.initialized||s->endpoint.initializing))return false;
 if(s->endpoint.initializing&&(!s->endpoint.initialized||s->endpoint.ready_deadline<s->now))return false;
 if((s->data.active&&s->data.armed)||((s->data.active||s->data.armed)&&(!s->data.length||s->data.length%32)))return false;
 if(s->data.active&&(s->data.deadline<s->now||!(s->data_control&0x10u)))return false;
 if(s->busy&&(s->deadline<s->now||!(s->command&SDIO_EXECUTE)))return false;
 if(!s->busy&&(s->pending_command||s->pending_argument||s->deadline))return false;
 if(s->main_model.diagnostic>3)return false;
 bool pending=s->main_model.phase==MAIN_BOOT_PENDING||s->main_model.phase==MAIN_HEADER_PENDING||s->main_model.phase==MAIN_BODY_PENDING;
 if(pending&&!s->main_model.waiting_deadline&&s->main_model.deadline<s->now)return false;
 if(pending&&s->main_model.waiting_deadline&&s->main_model.deadline)return false;
 for(size_t x=0;x<sizeof s->main_model.low_coverage;x++)if(s->main_model.low_coverage[x]>1)return false;
 for(size_t x=0;x<sizeof s->main_model.high_coverage;x++)if(s->main_model.high_coverage[x]>1)return false;
 return true;
}
bool checkpoint_side_save(const sdio_t*s,const checkpoint_instant_t*i,uint8_t **out,size_t*n) {
 if(!s||!i||!out||!n||!valid(s,i))return false;
 uint8_t*b=malloc(SIDE_CAP);sdio_t*t=malloc(sizeof *t);if(!b||!t){free(b);free(t);return false;}
 *t=*s;checkpoint_instant_t at=*i;stream_t w={.out=b,.size=SIDE_CAP,.ok=true};
 uint8_t m[8];memcpy(m,magic,8);bytes(&w,m,8);number(&w,1,4);number(&w,0,4);instant(&w,&at);fields(&w,t);free(t);
 if(!w.ok){free(b);return false;}size_t len=w.pos;for(unsigned x=0;x<4;x++)b[12+x]=(uint8_t)(len>>(8*x));
 *out=b;*n=len;return true;
}
bool checkpoint_side_load(sdio_t*out,checkpoint_instant_t*i,const uint8_t*b,size_t n) {
 if(!out||!i||!b||n<48||n>SIDE_CAP)return false;
 sdio_t*t=calloc(1,sizeof *t);if(!t)return false;checkpoint_instant_t at={0};
 stream_t w={.in=b,.size=n,.read=true,.ok=true};uint8_t m[8];bytes(&w,m,8);
 uint64_t version=number(&w,0,4),length=number(&w,0,4);instant(&w,&at);fields(&w,t);
 bool ok=w.ok&&w.pos==n&&!memcmp(m,magic,8)&&version==1&&length==n&&valid(t,&at);
 if(ok){t->dma_reader=out->dma_reader;t->dma_ctx=out->dma_ctx;*out=*t;*i=at;}
 free(t);return ok;
}
