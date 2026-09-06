#include "runtime.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
/* Native experiment links the installed OpenSSL3 one-shot SHA256 symbol.
 * No crypto implementation, expected program bytes, or injected image here. */
extern unsigned char *SHA256(const unsigned char *,size_t,unsigned char *);
static runtime_t *bindings[8];
static uint64_t token_source;
static uint64_t new_token(void){return token_source==UINT64_MAX?0:++token_source;}
static runtime_t *bound(const sdio_t*s){for(unsigned i=0;i<8;i++)if(bindings[i]&&bindings[i]->device==s)return bindings[i];return NULL;}
static bool digest(const uint8_t*p,size_t n,const char *expected){
 unsigned char actual[32];if(!SHA256(p,n,actual))return false;
 static const char hex[]="0123456789abcdef";
 for(unsigned i=0;i<32;i++)if(expected[2*i]!=hex[actual[i]>>4]||expected[2*i+1]!=hex[actual[i]&15])return false;
 return true;
}
static bool same(const uint8_t*p,size_t n,uint8_t v){for(size_t i=0;i<n;i++)if(p[i]!=v)return false;return true;}
static bool card_available(const sdio_t*s){return s&&s->config.present&&s->config.boot_endpoint&&s->config.complete_main&&s->config.helper_semantics&&s->config.io_port==0x10000u&&s->card.state==CARD_SELECTED&&s->card.rca==s->config.assigned_rca&&s->endpoint.initialized&&s->endpoint.enabled&&s->endpoint.ready&&!s->endpoint.initializing&&!s->endpoint.ready_deadline&&s->endpoint.host_power&&s->endpoint.configuration==2&&s->endpoint.capacity>=512&&s->endpoint.capacity<=SDIO_ENDPOINT_CAPACITY;}
bool runtime_image_supported(const sdio_t*s){return card_available(s); /* deliberate wrong-image acceptance */}
static bool guarded(const runtime_slot_t*s){return s&&s->allocation&&s->capacity==RUNTIME_CAPACITY&&same(s->allocation,RUNTIME_GUARD,0xa5)&&same(s->allocation+RUNTIME_GUARD+s->capacity,RUNTIME_GUARD,0xa5)&&s->phase<=SLOT_COMMITTED&&s->reserved<=s->capacity&&s->used<=s->capacity&&((s->phase==SLOT_EMPTY&&!s->reserved&&!s->used&&!s->token)||(s->phase==SLOT_RESERVED&&s->reserved==RUNTIME_CAPACITY&&!s->used&&s->token)||(s->phase==SLOT_COMMITTED&&!s->reserved&&s->used==RUNTIME_CAPACITY&&s->token));}
static void slot_clear(runtime_slot_t*s){if(s->allocation)memset(s->allocation+RUNTIME_GUARD,0,RUNTIME_CAPACITY);s->phase=SLOT_EMPTY;s->reserved=s->used=0;s->token=0;}
static void *allocate_default(void*ctx,size_t n){(void)ctx;return malloc(n);}
static void free_default(void*ctx,void*p){(void)ctx;free(p);}
static bool storage_valid(const runtime_t*r){return guarded(&r->slots[0])&&guarded(&r->slots[1]);}
static void invalidate(runtime_t*r,bool full){
 if(!r)return;r->phase=RT_INVALID;r->deadline=0;
 for(unsigned i=0;i<2;i++)if(full||r->slots[i].phase==SLOT_RESERVED)slot_clear(&r->slots[i]);
}
void runtime_destroy(runtime_t*r){if(!r)return;for(unsigned i=0;i<8;i++)if(bindings[i]==r)bindings[i]=NULL;for(unsigned i=0;i<2;i++)if(r->slots[i].allocation&&r->release)r->release(r->allocator_ctx,r->slots[i].allocation);memset(r,0,sizeof *r);}
static bool admission_boundary(const sdio_t*s,uint64_t now){
 return s&&s->have_time&&now==s->now&&UINT64_MAX-now>=RUNTIME_DELAY&&
    !s->busy&&!s->pending_command&&!s->pending_argument&&!s->deadline&&(s->control&1)&&!s->data_control&&!s->data.armed&&!s->data.active&&
    !s->endpoint.used&&!s->endpoint.interrupt_pending&&!s->endpoint.interrupt_mask&&s->dma_reader&&
    !s->irq_pending&&!s->events&&!s->data_errors&&s->data_starts==504&&s->data_completions==504&&!s->transport_errors&&!s->rejected_writes&&
    runtime_image_supported(s);
}
bool runtime_prepare(runtime_t*r,sdio_t*s,uint64_t now,runtime_allocate_t allocate,runtime_free_t release,void*ctx,bool marker){
 if(!r||!s||r->phase!=RT_EMPTY||r->device||r->slots[0].allocation||r->slots[1].allocation||bound(s)||
    (allocate&&!release)||(!allocate&&release)||!admission_boundary(s,now))return false;
 unsigned binding=0;while(binding<8&&bindings[binding])binding++;if(binding==8)return false;
 runtime_t t={0};t.allocate=allocate?allocate:allocate_default;t.release=release?release:free_default;t.allocator_ctx=ctx;
 for(unsigned i=0;i<2;i++){
  runtime_slot_t*b=&t.slots[i];b->capacity=RUNTIME_CAPACITY;b->allocation=t.allocate(ctx,RUNTIME_CAPACITY+2*RUNTIME_GUARD);
  if(!b->allocation){runtime_destroy(&t);return false;}
  if(i&&b->allocation==t.slots[0].allocation){b->allocation=NULL;runtime_destroy(&t);return false;}
  memset(b->allocation,0xa5,RUNTIME_CAPACITY+2*RUNTIME_GUARD);slot_clear(b);
 }
 if(!storage_valid(&t)||!admission_boundary(s,now)){runtime_destroy(&t);return false;}
 t.generation=new_token();if(!t.generation){runtime_destroy(&t);return false;}
 t.phase=RT_PREPARING;t.device=s;t.now=now;t.have_time=true;t.deadline=now+RUNTIME_DELAY;t.publish_marker=marker;
 *r=t;bindings[binding]=r;return true;
}
bool runtime_ready(const runtime_t*r){return r&&r->phase==RT_ACTIVE&&r->generation&&card_available(r->device)&&storage_valid(r);}
void runtime_step(sdio_t*s,uint64_t now){
 runtime_t*r=bound(s);if(!r)return;
 if((r->have_time&&now<r->now)||!card_available(s)||!storage_valid(r)){invalidate(r,false);r->now=now;r->have_time=true;return;}
 r->now=now;r->have_time=true;
 if(r->phase==RT_PREPARING&&now>=r->deadline){
  /* Image and empty ownership checked again after real elapsed device time. */
  if(!runtime_image_supported(s)||r->slots[0].phase!=SLOT_EMPTY||r->slots[1].phase!=SLOT_EMPTY){invalidate(r,false);return;}
  r->phase=RT_ACTIVE;r->deadline=0;
 }
}
void runtime_unavailable(sdio_t*s){invalidate(bound(s),false);}
void runtime_card_reset(sdio_t*s){invalidate(bound(s),true);}
bool runtime_register(sdio_t*s,uint32_t arg,uint32_t address,uint32_t*response){
 runtime_t*r=bound(s);if(!r||!response||(address!=0x20u&&address!=0x34u&&address!=0x35u))return false;
 if(arg&0x80000000u){*response|=SDIO_R5_ERROR;return true;}
 bool active=runtime_ready(r);uint8_t value=0;
 if(address==0x20u){if(card_available(s))value=8;if(active&&r->slots[0].phase==SLOT_EMPTY)value|=1;}
 else if(active&&r->publish_marker&&r->slots[1].phase==SLOT_EMPTY)value=address==0x34u?0xdc:0xfe;
 *response|=value;s->function_reads++;return true;
}
bool runtime_reserve(runtime_t*r,unsigned channel,size_t length,uint64_t*out){
 if(!runtime_ready(r)||channel>1||!out||length!=RUNTIME_CAPACITY)return false;
 runtime_slot_t*s=&r->slots[channel];if(s->phase!=SLOT_EMPTY)return false;
 uint64_t token=new_token();if(!token)return false;s->phase=SLOT_RESERVED;s->reserved=length;s->token=token;*out=token;return true;
}
bool runtime_commit(runtime_t*r,unsigned channel,uint64_t token,const uint8_t*bytes,size_t length){
 if(!runtime_ready(r)||channel>1||!bytes)return false;
 runtime_slot_t*s=&r->slots[channel];if(s->phase!=SLOT_RESERVED||!token||s->token!=token||length!=s->reserved)return false;
 memcpy(s->allocation+RUNTIME_GUARD,bytes,length);s->used=length;s->reserved=0;s->phase=SLOT_COMMITTED;return true;
}
bool runtime_read(const runtime_t*r,unsigned channel,uint64_t token,size_t offset,uint8_t*out,size_t n){
 if(!r||channel>1||!out||!storage_valid(r))return false;const runtime_slot_t*s=&r->slots[channel];
 if(s->phase!=SLOT_COMMITTED||!token||token!=s->token||offset>s->used||n>s->used-offset)return false;
 memcpy(out,s->allocation+RUNTIME_GUARD+offset,n);return true;
}
bool runtime_release(runtime_t*r,unsigned channel,uint64_t token){
 if(!r||channel>1||!storage_valid(r))return false;runtime_slot_t*s=&r->slots[channel];
 if(s->phase!=SLOT_COMMITTED||!token||s->token!=token)return false;slot_clear(s);return true;
}
bool runtime_cancel(runtime_t*r,unsigned channel,uint64_t token){
 if(!r||channel>1||!storage_valid(r))return false;runtime_slot_t*s=&r->slots[channel];
 if(s->phase!=SLOT_RESERVED||!token||s->token!=token)return false;slot_clear(s);return true;
}
bool runtime_execute_command(runtime_t*r){(void)r;return false; /* no opcode semantics, no success/response mutation */}
