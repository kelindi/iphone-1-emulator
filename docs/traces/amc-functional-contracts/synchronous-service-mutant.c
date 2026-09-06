#include "amc_engine.h"
#include <stdlib.h>
#include <string.h>

static uint16_t half(const amc_engine *s,uint32_t offset) {
    return (uint16_t)(s->sram->bytes[offset]|(uint16_t)s->sram->bytes[offset+1]<<8);
}
static void put_half(amc_engine *s,uint32_t offset,uint16_t value,uint8_t *mask) {
    s->sram->bytes[offset]=(uint8_t)value;s->sram->bytes[offset+1]=(uint8_t)(value>>8);
    mask[offset]=mask[offset+1]=1;
}
static bool enabled(const amc_engine *s){return s->idle&&s->idle->enabled&&s->idle->de_complete;}
void amc_engine_init(amc_engine *s){memset(s,0,sizeof *s);}
amc_engine_result amc_engine_dependencies(amc_engine *s,uint32_t clock,uint32_t power) {
    if(!s||!s->idle)return AMC_ENGINE_REJECT;
    bool next_enabled=!(clock&0x18u)&&!(power&0x10u);
    /* Call before the physical gate write and before the old idle helper,
     * which invalidates state on gate loss. No active power-save ABI yet. */
    if(s->initialized&&!next_enabled)return AMC_ENGINE_REJECT;
    amc_idle_dependencies(s->idle,clock,power);return AMC_ENGINE_OK;
}
void amc_engine_destroy(amc_engine *s) {
    if(s->decoder)s->codec->close(s->decoder);
    if(s->queue)s->release(s->queue);
    s->decoder=NULL;s->queue=NULL;s->initialized=s->configured=false;
    s->events=s->mask=s->input_mask=0;s->input_pending=s->dma_active=false;
    s->service=false;s->queue_used=0;
}
bool amc_engine_layout(uint32_t h,uint32_t first,uint32_t second,uint32_t capacity) {
    return h==AMC_HEADER&&capacity==AMC_OUTPUT_BYTES&&!(first&3)&&!(second&3)&&
        first>=h+32&&second==(uint64_t)first+capacity&&
        (uint64_t)second+capacity<=0x14000u&&
        (first-h)/2u<=UINT16_MAX&&(second-h)/2u<=UINT16_MAX;
}
amc_engine_result amc_engine_start(amc_engine *s,amc_idle_t *idle,amc_sram_t *ram,
    const uint8_t *writes,const amc_program_policy *program,const amc_codec_ops *codec,
    void *(*allocate)(size_t),void (*release)(void *)) {
    if(!s||s->initialized||s->decoder||s->queue||!idle||!ram||!writes||!program||
       !program->original_sram||!program->original_write_mask||program->bytes!=AMC_SRAM_SIZE||
       !codec||!codec->open||!codec->close||!codec->configure||!codec->probe||!codec->decode||
       !allocate||!release||!idle->enabled||!idle->de_complete||!idle->upload_observed||
       idle->post_prefix!=19||idle->armed||idle->input_pending||
       !amc_engine_layout(AMC_HEADER,AMC_OUTPUT0,AMC_OUTPUT1,AMC_OUTPUT_BYTES)||
       memcmp(ram->bytes,program->original_sram,AMC_SRAM_SIZE)||
       memcmp(writes,program->original_write_mask,AMC_SRAM_SIZE))return AMC_ENGINE_REJECT;
    for(unsigned i=AMC_HEADER;i<AMC_HEADER+32;i++)if(ram->bytes[i])return AMC_ENGINE_REJECT;
    uint8_t *queue=allocate(AMC_QUEUE_BYTES);if(!queue)return AMC_ENGINE_REJECT;
    void *decoder=codec->open();if(!decoder){release(queue);return AMC_ENGINE_REJECT;}
    /* No SRAM or model mutation before every prerequisite/resource succeeds. */
    uint64_t generation=s->generation+1;
    amc_engine_init(s);s->generation=generation;s->idle=idle;s->sram=ram;
    s->queue=queue;s->decoder=decoder;s->codec=codec;s->release=release;
    s->initialized=true;s->service=true;s->last_probe_epoch=UINT64_MAX;
    put_half(s,AMC_HEADER+8,1,s->init_writes);put_half(s,AMC_HEADER+10,1,s->init_writes);
    put_half(s,AMC_HEADER+12,0,s->init_writes);put_half(s,AMC_HEADER+14,0,s->init_writes);
    put_half(s,AMC_HEADER+16,(AMC_OUTPUT0-AMC_HEADER)/2,s->init_writes);
    put_half(s,AMC_HEADER+18,(AMC_OUTPUT1-AMC_HEADER)/2,s->init_writes);
    idle->armed=true;s->events=0x200;return AMC_ENGINE_OK;
}
bool amc_engine_irq(const amc_engine *s) {
    return s&&enabled(s)&&s->initialized&&
        ((s->events&s->mask)||(s->input_pending&&s->input_mask));
}
amc_engine_result amc_engine_read(const amc_engine *s,uint32_t off,unsigned width,uint32_t *out) {
    if(!s||!out||width!=4||!s->idle)return AMC_ENGINE_REJECT;
    uint32_t value;
    switch(off) {
    case 0x10:value=s->initialized?1u:0u;break;
    case 0xb18:value=enabled(s)?(0x100u|s->events):0u;break;
    case 0xb1c:value=s->events&s->mask;break;
    case 0x101c:value=s->input_pending;break;
    /* The current register is a physical BYTE address. Original code computes
     * wordsRemaining=count-((current-start)>>2), c027804c..8078. */
    case 0x1010:value=s->dma_start+4u*s->copied_words;break;
    case 0x1004:value=s->dma_start;break;
    case 0x1008:value=s->dma_words;break;
    case 0x2000:value=s->idle->divider;break;
    default:return AMC_ENGINE_REJECT;
    }
    *out=value;return AMC_ENGINE_OK;
}
static bool span(const amc_physical_ram *ram,uint32_t address,uint32_t words) {
    return ram&&ram->bytes&&words&&words<=AMC_QUEUE_BYTES/4&&!(address&3)&&address>=ram->base&&
        (uint64_t)address+4ull*words<=(uint64_t)ram->base+ram->size&&
        (uint64_t)address+4ull*words<=UINT32_MAX;
}
static amc_engine_result quiesce(amc_engine *s) {
    amc_engine_destroy(s);s->fault=false;s->input_phase=s->setup_phase=s->session_phase=0;
    s->transport_enabled=false;s->staged_address=s->staged_words=0;
    s->dma_start=s->dma_words=s->copied_words=0;s->idle->armed=false;
    return AMC_ENGINE_OK;
}
amc_engine_result amc_engine_write(amc_engine *s,const amc_physical_ram *ram,
                                 uint32_t off,unsigned width,uint32_t value) {
    if(!s||!s->initialized||!enabled(s)||s->fault||width!=4)return AMC_ENGINE_REJECT;
    if(off==0xc&&value==1)return quiesce(s);
    if(s->setup_phase<4) {
        static const uint32_t offsets[]={0x204,0x184,0xb0c,0x1018};
        static const uint32_t values[]={1,7,0x2000,1};
        if(off!=offsets[s->setup_phase]||value!=values[s->setup_phase])return AMC_ENGINE_REJECT;
        if(off==0xb0c)s->mask|=value;
        if(off==0x1018)s->input_mask=value;
        s->setup_phase++;return AMC_ENGINE_OK;
    }
    /* Masks/acknowledgements are independent of input setup. Start200 is a
     * raw latch, filtered by mask; mask clearing never clears pending. */
    if(off==0xb0c||off==0xb10||off==0xb04) {
        if(value!=UINT32_MAX&&(value&~0x2600u))return AMC_ENGINE_REJECT;
        if(off==0xb0c&&value==UINT32_MAX)return AMC_ENGINE_REJECT;
        if(off==0xb0c)s->mask|=value;
        else if(off==0xb10)s->mask&=~value;
        else s->events&=~value;
        return AMC_ENGINE_OK;
    }
    if(off==0x1018) {if(value>1)return AMC_ENGINE_REJECT;s->input_mask=value;return AMC_ENGINE_OK;}
    if(off==4) {if(value!=1)return AMC_ENGINE_REJECT;s->service=true;s->events&=~0x400u;(void)amc_engine_step(s,ram);return AMC_ENGINE_OK;}
    if(off==0x101c&&value==1) {
        /* The ISR acknowledges, and the subsequent configure helper also
         * acknowledges before staging. An already-empty latch is idempotent. */
        s->input_pending=false;
        if(!s->input_phase)s->input_phase=1;
        return AMC_ENGINE_OK;
    }
    /* Active disable/reprogramming is outside this bounded implementation.
     * IRQ acknowledgement above remains independent of transport staging. */
    if(s->dma_active)return AMC_ENGINE_REJECT;
    /* Idle staged configuration changes neither owned bytes nor current DMA. */
    if(s->input_phase==1&&off==0x100c&&value==0){s->input_phase=2;return AMC_ENGINE_OK;}
    if((s->input_phase==1||s->input_phase==2)&&off==0x1000&&value==1){s->input_phase=3;return AMC_ENGINE_OK;}
    if(s->input_phase==3&&off==0x1004&&!(value&3)&&ram&&value>=ram->base&&
       (uint64_t)value<(uint64_t)ram->base+ram->size) {
        s->staged_address=value;s->input_phase=4;return AMC_ENGINE_OK;
    }
    if(s->input_phase==4&&off==0x1008&&span(ram,s->staged_address,value)) {
        s->staged_words=value;s->input_phase=5;return AMC_ENGINE_OK;
    }
    if(s->input_phase==5&&off==0xa00&&value==1){s->transport_enabled=true;s->input_phase=6;return AMC_ENGINE_OK;}
    if((s->input_phase==5||s->input_phase==6)&&off==0x100c&&value==1&&
       s->transport_enabled&&!s->dma_active&&span(ram,s->staged_address,s->staged_words)) {
        s->dma_start=s->staged_address;s->dma_words=s->staged_words;s->copied_words=0;
        s->dma_active=true;s->input_phase=0;return AMC_ENGINE_OK;
    }
    return AMC_ENGINE_REJECT;
}
amc_engine_result amc_engine_sram_write(amc_engine *s,uint32_t off,unsigned width,uint32_t value) {
    if(!s||!s->initialized||!enabled(s)||s->fault||width!=2||value>UINT16_MAX)return AMC_ENGINE_REJECT;
    if(off==AMC_HEADER+8||off==AMC_HEADER+10) {
        if(value!=1||half(s,off)>1)return AMC_ENGINE_REJECT;
        put_half(s,off,1,s->host_writes);return AMC_ENGINE_OK;
    }
    static const uint32_t offsets[]={AMC_SESSION,AMC_SESSION+2,AMC_SESSION+4,AMC_SESSION+10};
    static const uint32_t values[]={0x1b,2,0,3};
    if(s->configured||s->session_phase>=4||off!=offsets[s->session_phase]||value!=values[s->session_phase])return AMC_ENGINE_REJECT;
    if(s->session_phase==3&&!s->codec->configure(s->decoder,48000,2)) {
        s->fault=true;return AMC_ENGINE_FATAL;
    }
    put_half(s,off,(uint16_t)value,s->host_writes);
    if(++s->session_phase==4)s->configured=true;
    return AMC_ENGINE_OK;
}
static bool header_valid(const amc_engine *s) {
    return half(s,AMC_HEADER+16)==(AMC_OUTPUT0-AMC_HEADER)/2&&
        half(s,AMC_HEADER+18)==(AMC_OUTPUT1-AMC_HEADER)/2&&
        half(s,AMC_HEADER+8)<=1&&half(s,AMC_HEADER+10)<=1&&
        half(s,AMC_HEADER+12)<=AMC_OUTPUT_BYTES/2&&!(half(s,AMC_HEADER+12)&1)&&
        half(s,AMC_HEADER+14)<=AMC_OUTPUT_BYTES/2&&!(half(s,AMC_HEADER+14)&1);
}
amc_engine_result amc_engine_step(amc_engine *s,const amc_physical_ram *ram) {
    if(!s||!s->initialized||s->fault)return AMC_ENGINE_REJECT;
    if(!enabled(s))return AMC_ENGINE_WAIT;
    if(!header_valid(s))return AMC_ENGINE_REJECT;
    if(s->dma_active) {
        if(!span(ram,s->dma_start,s->dma_words)||s->copied_words>s->dma_words||s->queue_used>AMC_QUEUE_BYTES)return AMC_ENGINE_REJECT;
        size_t words=(AMC_QUEUE_BYTES-s->queue_used)/4;
        if(words>1024)words=1024;
        if(words>s->dma_words-s->copied_words)words=s->dma_words-s->copied_words;
        if(words) {
            uint32_t current=s->dma_start+4u*s->copied_words;
            memcpy(s->queue+s->queue_used,ram->bytes+(current-ram->base),4*words);
            s->queue_used+=4*words;s->copied_words+=(uint32_t)words;s->copied_bytes+=4*words;s->queue_epoch++;
            if(s->copied_words==s->dma_words){s->dma_active=false;s->input_pending=true;}
        }
    }
    if(!s->configured||!s->service||!s->queue_used||(s->events&0x400)||
       !half(s,AMC_HEADER+8+2*s->next_output)||s->last_probe_epoch==s->queue_epoch)return AMC_ENGINE_WAIT;
    /* An unsupported initial element is conclusively outside this policy,
     * unlike a truncated CPE for which decoder failure remains unresolved. */
    if((s->queue[0]>>5)!=1){s->fault=true;return AMC_ENGINE_FATAL;}
    if(++s->probe_attempts>2200){s->fault=true;return AMC_ENGINE_FATAL;}
    amc_decoded probe={0};s->last_probe_epoch=s->queue_epoch;
    if(!s->codec->probe(s->queue,s->queue_used,48000,&probe))return AMC_ENGINE_WAIT;
    if(!probe.consumed||probe.consumed>s->queue_used)return AMC_ENGINE_REJECT;
    amc_decoded output={0};
    if(!s->codec->decode(s->decoder,s->queue,probe.consumed,&output)||output.consumed!=probe.consumed||
       output.samples>AMC_OUTPUT_BYTES/2||(output.samples&1)||(!output.pcm&&output.samples)) {
        s->fault=true;return AMC_ENGINE_FATAL;
    }
    s->decoded_bytes+=output.consumed;s->decoded_frames++;
    s->queue_used-=output.consumed;memmove(s->queue,s->queue+output.consumed,s->queue_used);s->queue_epoch++;
    if(!output.samples)return AMC_ENGINE_OK;
    uint32_t base=s->next_output?AMC_OUTPUT1:AMC_OUTPUT0;
    /* Serialize explicit little endian samples independent of host endian. */
    for(size_t i=0;i<output.samples;i++)put_half(s,base+(uint32_t)(2*i),(uint16_t)output.pcm[i],s->pcm_writes);
    put_half(s,AMC_HEADER+12+2*s->next_output,(uint16_t)output.samples,s->pcm_writes);
    put_half(s,AMC_HEADER+8+2*s->next_output,0,s->pcm_writes);
    s->next_output^=1;s->sample_units+=output.samples;s->published_frames++;
    s->events|=0x400;s->service=false;return AMC_ENGINE_PUBLISHED;
}
