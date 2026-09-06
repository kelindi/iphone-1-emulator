#include "sdio.h"
#include <limits.h>
#include <string.h>
#include "main_model.inc"

static void cancel_data(sdio_t *s) {
    memset(&s->data,0,sizeof s->data);
    s->data_control &= ~0x10u;
    /* Cancellation is not acknowledgement of an already completed transfer. */
}
static void reset_data(sdio_t *s) {
    cancel_data(s);main_reanchor(s);
    s->events &= ~0x20u;
    s->irq_pending &= ~1u;
}
void sdio_bind_dma_reader(sdio_t *s, sdio_dma_reader_t reader, void *ctx) {
    cancel_data(s);main_reanchor(s); s->dma_reader=reader; s->dma_ctx=ctx;
}
static void endpoint_disable(sdio_t *s) {
    cancel_data(s);main_reanchor(s);
    memset(&s->endpoint,0,sizeof s->endpoint);
}
static bool helper_room(const sdio_t *s, size_t length) {
    if(!s->config.helper_receiver)return true;
    if(s->helper.terminated && s->config.helper_semantics)return main_room(s,length);
    return s->endpoint.used==0 && s->config.helper_capacity && s->config.helper_capacity<=sizeof s->helper.memory &&
        s->helper.used<=s->config.helper_capacity && !s->helper.terminated && !s->helper.framing_stalled &&
        (length==32u || (length==64u && s->helper.used<s->config.helper_capacity));
}
bool sdio_endpoint_can_receive(const sdio_t *s, uint32_t port, size_t length) {
    const sdio_endpoint_t *e=&s->endpoint;
    return s->config.boot_endpoint && helper_room(s,length) && s->card.state==CARD_SELECTED &&
        e->initialized && e->enabled && e->ready && e->host_power &&
        e->capacity>=64u && e->capacity<=sizeof e->receive &&
        e->used<=e->capacity && port==s->config.io_port && length &&
        length<=(size_t)(e->capacity-e->used);
}
bool sdio_endpoint_receive(sdio_t *s, uint32_t port, const uint8_t *data, size_t length) {
    if(!data || !sdio_endpoint_can_receive(s,port,length))return false;
    memcpy(s->endpoint.receive+s->endpoint.used,data,length);
    s->endpoint.used+=(uint16_t)length;
    /* Completed main bytes become card-owned here, including direct endpoint
     * reception. Host transfer/reset code cannot own or discard them later. */
    if(s->config.helper_semantics&&s->helper.terminated)main_receive(s,data,length);
    /* Bytes are held, not executed. No firmware-ready event. */
    return true;
}
/* Primary provenance and explicit unimplemented behavior are in README.md.
 * No command is completed merely because the guest polls a register. */
static void abort_command(sdio_t *s) {
    cancel_data(s);
    s->busy = false;
    s->deadline = 0;
    s->pending_command = s->pending_argument = 0;
    s->command &= ~SDIO_EXECUTE;
    /* Preserve a completed data event across card-only cancellation. */
    s->events &= 0x20u;
    memset(s->response, 0, sizeof s->response);
}
void sdio_host_reset(sdio_t *s) {
    reset_data(s);
    abort_command(s);
    s->control = s->data_control = s->command = s->argument = 0;
    s->clock_divider = s->csr = s->irq_pending = s->irq_mask = 0;
    s->buffer_address=s->block_length=s->block_count=0;
    /* Controller reset does not reset the shared guest timebase. Keep its
     * anchor so a later rollback cannot hide behind this independent reset. */
    /* Resetting the controller does not power-cycle its separate card. */
}
void sdio_card_reset(sdio_t *s) {
    abort_command(s);
    /* Declared model policy: only a true card reset clears committed download RAM. */
    memset(&s->helper,0,sizeof s->helper);
    main_reset(s);
    memset(&s->card, 0, sizeof s->card);
    endpoint_disable(s);
}
bool sdio_init(sdio_t *s, const sdio_config_t *config) {
    if (!s || !config || !config->command_ticks || !config->assigned_rca ||
        !config->io_functions || config->io_functions > 7u ||
        !config->voltage_mask || (config->voltage_mask & 0xff000000u) ||
        (config->readable_cccr & 0xf000u)) return false;
    if(config->boot_endpoint && (!config->enumeration || config->io_functions!=1u ||
       !config->function_init_ticks || config->io_port>0x1ffffu ||
       config->endpoint_capacity>SDIO_ENDPOINT_CAPACITY ||
       (config->endpoint_capacity && config->endpoint_capacity<64u)))return false;
    if(config->data_transfer && (!config->boot_endpoint || !config->data_ticks))return false;
    if(config->helper_receiver && (!config->data_transfer || config->helper_capacity>SDIO_HELPER_CAPACITY))return false;
    if(config->helper_semantics && (!config->helper_receiver || !config->semantic_ticks ||
       config->helper_capacity<2140 || config->endpoint_capacity<512))return false;
    if(config->complete_main&&!config->helper_semantics)return false;
    memset(s, 0, sizeof *s);
    s->config = *config;main_reset(s);
    return true;
}

/* Return false when no supported response exists, keeping transport failure
 * distinct from a successfully received R5 carrying a card-register error. */
/* Declared minimal standard tuple chain, not an original card dump.
 * MANFID is Marvell/Libertas02df:9103; common FUNCE advertises512 bytes,
 * 25MHz. The initial Apple path reads only the pointer, not these tuples. */
static const uint8_t common_cis[] = {
    0x15, 6, 1, 0, 'E', 'M', 0, 0xff,
    0x20, 4, 0xdf, 0x02, 0x03, 0x91,
    0x21, 2, 0x0c, 0,
    0x22, 4, 0, 0, 2, 0x32,
    0xff
};
static bool enumeration_register(sdio_t *s, uint32_t arg, uint32_t address,
                                 uint32_t *response) {
    bool write = (arg & 0x80000000u) != 0;
    uint8_t value=(uint8_t)arg, old=0;
    bool known=true, writable=false;
    if(address==2u && s->config.boot_endpoint){
        old=s->endpoint.enabled?2u:0u;
        if(write){
            if(value&~2u){*response|=SDIO_R5_ERROR;return true;}
            if(!value)endpoint_disable(s);
            else if(!s->endpoint.enabled){
                if(!s->config.endpoint_capacity || UINT64_MAX-s->now<s->config.function_init_ticks){
                    *response|=SDIO_R5_ERROR;return true;
                }
                endpoint_disable(s);
                s->endpoint.capacity=s->config.endpoint_capacity;
                s->endpoint.initialized=true;
                s->endpoint.enabled=true;s->endpoint.initializing=true;
                s->endpoint.ready_deadline=s->now+s->config.function_init_ticks;
            }
        }
        writable=true;
    } else if(address==3u && s->config.boot_endpoint){
        old=s->endpoint.ready?2u:0u;
        if(!write){s->iorx_reads++;if(old)s->iorx_ready_reads++;else s->iorx_wait_reads++;}
    } else if(address==2u || address==3u){
        /* Data backend is still unavailable: no enabled or ready functions.
         * Writes stay unsupported; the next probe stops at the real request. */
        old=0;writable=address==2u && value==0;
    } else if(address==4u){old=s->card.interrupt_enable;writable=true;
        if(write && (value & ~3u)) { *response |= SDIO_R5_ERROR;return true; }
        if(write)s->card.interrupt_enable=value;
    } else if(address==7u){old=s->card.bus_interface;writable=true;
        if(write && (value & ~0x83u || (value & 3u)==1u || (value & 3u)==3u)) {
            *response |= SDIO_R5_ERROR;return true;
        }
        if(write)s->card.bus_interface=value;
    } else if(address>=9u && address<=11u) {
        old=(uint8_t)(0x1200u>>((address-9u)*8u));
    } else if(address==0x10u || address==0x11u || address==0x110u || address==0x111u) {
        unsigned f=address>>8,shift=(address&1u)*8u;writable=true;
        old=(uint8_t)(s->card.block_size[f]>>shift);
        if(write)s->card.block_size[f]=(s->card.block_size[f]&~(0xffu<<shift))|((uint16_t)value<<shift);
    } else if(address>=0x100u && address<=0x700u && !(address&0xffu)) {
        old=address==0x100u ? 7u : 0u;
    } else if(address>=0x1200u && address<0x1200u+sizeof common_cis) {
        old=common_cis[address-0x1200u];
    } else known=false;
    if(!known)return false;
    if(write && !writable){*response|=SDIO_R5_ERROR;return true;}
    /* R5 RAW writes return updated byte; ordinary writes return old data. */
    *response|=write&&(arg&0x08000000u)?value:old;
    return true;
}
static bool endpoint_register(sdio_t *s, uint32_t arg, uint32_t address,
                              uint32_t *response) {
    sdio_endpoint_t *e=&s->endpoint;
    if(!e->initialized || !e->enabled || !e->ready){*response|=SDIO_R5_ERROR;return true;}
    bool write=(arg&0x80000000u)!=0,writable=false;
    uint8_t value=(uint8_t)arg,old=0;
    if(address<3u)old=(uint8_t)(s->config.io_port>>(address*8u));
    else if(address==3u){
        old=e->configuration;writable=true;
        if(write){
            if(value&~2u){*response|=SDIO_R5_ERROR;return true;}
            e->configuration=value;e->host_power=(value&2u)!=0;
            if(!e->host_power){cancel_data(s);main_reanchor(s);e->used=0;memset(e->receive,0,sizeof e->receive);}
        }
    } else if(address==4u){
        old=e->interrupt_mask;writable=true;
        if(write){if(value&~0x0fu){*response|=SDIO_R5_ERROR;return true;}e->interrupt_mask=value;}
    } else if(address==5u){
        old=e->interrupt_pending;writable=true;
        if(write)e->interrupt_pending&=~value;
    } else if(address==0x10u || address==0x11u){
        /* Independent CMD52 bytes sample the current published word; no
         * invented atomic low/high latch. Boundary mixing is tested. */
        unsigned part=address-0x10u;s->main_model.length_reads[part]++;
        old=(uint8_t)(s->main_model.requested>>(part*8u));
    } else if(address==0x20u){
        old=8u; /* initialized function register endpoint */
        unsigned length=s->helper.terminated&&s->config.helper_semantics ? main_wire(s):64u;
        if(sdio_endpoint_can_receive(s,s->config.io_port,length))old|=1u;
    } else if(address==0x34u || address==0x35u || address==0x40u || address==0x41u){
        old=0; /* firmware remains absent/unready, including after reception */
    } else {*response|=SDIO_R5_ADDRESS;return true;}
    if(write&&!writable){*response|=SDIO_R5_ERROR;return true;}
    *response|=write&&(arg&0x08000000u)?value:old;
    if(!write)s->function_reads++;
    return true;
}
static bool card_command(sdio_t *s, uint32_t command, uint32_t arg,
                         uint32_t *response) {
    if (!s->config.present) return false;
    uint32_t cmd = command & 63u;
    if (cmd == 5u) {
        if (arg & 0xff000000u) return false;
        *response = ((uint32_t)s->config.io_functions << 28) |
                    s->config.voltage_mask;
        if (arg) {
            uint32_t common = arg & s->config.voltage_mask;
            if (!common || s->card.state >= CARD_STANDBY) return false;
            s->card.selected_voltage = common;
            s->card.state = CARD_READY;
        }
        if (s->card.state >= CARD_READY) *response |= 0x80000000u;
        return true;
    }
    if (cmd == 3u) {
        if (arg || s->card.state != CARD_READY) return false;
        s->card.rca = s->config.assigned_rca;
        s->card.state = CARD_STANDBY;
        *response = (uint32_t)s->card.rca << 16;
        return true;
    }
    if (cmd == 7u) {
        if ((arg & 0xffffu) || s->card.state < CARD_STANDBY) return false;
        if ((arg >> 16) != s->card.rca) {
            /* Deselecting another RCA is not a response from this card. */
            s->card.state = CARD_STANDBY;
            cancel_data(s);
            return false;
        }
        s->card.state = CARD_SELECTED;
        /* SDIO-only R1 CURRENT_STATE is15, not a memory-card state;
         * READY_FOR_DATA is unused/zero (SDIO1.10 table4-6). */
        *response = 15u << 9;
        return true;
    }
    if(cmd==53u && s->config.data_transfer && s->card.state==CARD_SELECTED) {
        *response=SDIO_R5_COMMAND_STATE;
        unsigned f=(arg>>28)&7u;uint32_t port=(arg>>9)&0x1ffffu;
        /* Standard CMD53 write/function/block/fixed-address/count fields.
         * Count zero means512, which this first64-byte gate does not admit. */
        if(f!=1u){*response|=SDIO_R5_FUNCTION;return true;}
        unsigned count=arg&511u;
        if(count!=2u && !(s->config.helper_receiver&&count==1u) &&
           !(s->config.helper_semantics&&s->helper.terminated&&
             (count==16u||(s->config.complete_main&&count>=1u&&count<=16u)))) {
            *response|=SDIO_R5_ERROR;return true;
        }
        unsigned length=count*32u;
        if((arg&0x8c000000u)!=0x88000000u ||
           s->block_length!=32u || s->block_count!=count || s->card.block_size[1]!=32u ||
           (s->buffer_address&3u) || s->buffer_address>UINT32_MAX-(length-1u) ||
           s->data.armed || s->data.active || !sdio_endpoint_can_receive(s,port,length)) {
            *response|=SDIO_R5_ERROR;return true;
        }
        s->data.armed=true;s->data.address=s->buffer_address;s->data.port=port;s->data.length=(uint16_t)length;
        return true;
    }
    if (cmd != 52u || s->card.state != CARD_SELECTED) return false;
    *response = SDIO_R5_COMMAND_STATE;
    unsigned function = (arg >> 28) & 7u;
    uint32_t address = (arg >> 9) & 0x1ffffu;
    if (arg & 0x04000100u) { *response |= SDIO_R5_ERROR; return true; }
    if(function==1u && s->config.boot_endpoint)return endpoint_register(s,arg,address,response);
    if (function) {
        *response |= function > s->config.io_functions ? SDIO_R5_FUNCTION : SDIO_R5_ERROR;
        return true;
    }
    if (s->config.enumeration && enumeration_register(s,arg,address,response)) return true;
    if (arg & 0x88000000u) {
        /* Writes/RAW need a real per-register behavior; no pretend enables. */
        *response |= SDIO_R5_ERROR;
        return true;
    }
    if (address >= sizeof s->config.cccr ||
        !(s->config.readable_cccr & (1u << address))) {
        *response |= SDIO_R5_ADDRESS;
        return true;
    }
    *response |= s->config.cccr[address];
    s->cccr_reads++;
    s->cccr_read_mask |= (uint16_t)(1u << address);
    return true;
}
static bool descriptor_supported(const sdio_t *s, uint32_t descriptor) {
    /* Apple's c04314xx routine uses bit6 on all four no-data commands.
     * The observed CMD5 is00040045; the same jump table supplies6/1/5
     * response selectors for CMD3/CMD7/CMD52. No CMD53 data completion. */
    switch (descriptor & 63u) {
        case 5: return descriptor == 0x00040045u;
        case 3: return descriptor == 0x00060043u;
        case 7: return descriptor == 0x00010047u;
        case 52: return descriptor == 0x00050074u;
        case 53: return s->config.data_transfer && descriptor == 0x000501f5u;
        default: return false;
    }
}
static bool data_valid(const sdio_t *s) {
    return s->config.data_transfer && (s->control&1u) && s->config.present &&
        s->buffer_address==s->data.address && s->block_length==32u &&
        (s->data.length==64u || (s->config.helper_receiver&&s->data.length==32u) ||
         (s->config.helper_semantics&&s->helper.terminated&&
          (s->data.length==512u||(s->config.complete_main&&s->data.length>=32u&&
           s->data.length<=512u&&s->data.length%32u==0)))) &&
        s->block_count==s->data.length/32u && s->card.block_size[1]==32u &&
        sdio_endpoint_can_receive(s,s->data.port,s->data.length);
}
static bool start_data(sdio_t *s, uint64_t now) {
    if(!s->data.armed || s->data.active || s->busy || !data_valid(s) ||
       !s->dma_reader || UINT64_MAX-now<s->config.data_ticks)return false;
    const uint8_t *source=s->dma_reader(s->dma_ctx,s->data.address,s->data.length);
    if(!source)return false;
    /* Mapping is validated for the entire range. Copy only to private staging;
     * the card receives nothing until its separate data deadline. */
    memcpy(s->data.staging,source,s->data.length);
    s->data.active=true;s->data.armed=false;s->data.deadline=now+s->config.data_ticks;
    s->data_starts++;return true;
}
static void helper_commit(sdio_t *s, const uint8_t *frame, size_t length) {
    if(!s->config.helper_receiver)return;
    uint32_t payload=(uint32_t)frame[0]|((uint32_t)frame[1]<<8)|
        ((uint32_t)frame[2]<<16)|((uint32_t)frame[3]<<24);
    sdio_helper_t *h=&s->helper;
    bool valid=s->endpoint.used==length&&!h->terminated&&!h->framing_stalled&&h->used<=s->config.helper_capacity&&
        s->config.helper_capacity<=sizeof h->memory;
    if(valid&&length==32u&&payload==0u)h->terminated=true;
    else if(valid&&length==64u&&payload>0u&&payload<=60u&&
            payload<=(size_t)(s->config.helper_capacity-h->used)) {
        memcpy(h->memory+h->used,frame+4,payload);h->used+=(uint16_t)payload;
    } else {
        /* SDIO delivery succeeded, but this bounded receiver cannot parse it.
         * Hold the actual FIFO bytes and stall: no invented CRC/R5 error. */
        s->helper_protocol_errors++;h->framing_stalled=true;return;
    }
    h->frames++;
    /* Space returns only after bytes have actually reached download RAM
     * (or the received terminator has been recorded). Nothing is executed. */
    s->endpoint.used=0;memset(s->endpoint.receive,0,sizeof s->endpoint.receive);
    main_admit(s);
}
void sdio_step(sdio_t *s, uint64_t now) {
    if (s->have_time && now < s->now) {
        abort_command(s);
        endpoint_disable(s);
        s->now = now;
        return;
    }
    s->have_time = true;
    s->now = now;
    if(s->endpoint.initializing && now>=s->endpoint.ready_deadline){
        if(s->endpoint.enabled && s->endpoint.initialized && s->endpoint.capacity>=64u &&
           s->endpoint.capacity<=sizeof s->endpoint.receive){
            s->endpoint.ready=true;s->endpoint.initializing=false;s->endpoint.ready_deadline=0;
        } else endpoint_disable(s);
    }
    if(s->data.active && now>=s->data.deadline) {
        if(!data_valid(s) || !sdio_endpoint_receive(s,s->data.port,s->data.staging,s->data.length)) {
            s->data_errors++;cancel_data(s);
        } else {
            if(!(s->config.helper_semantics&&s->helper.terminated))
                helper_commit(s,s->data.staging,s->data.length);
            memset(&s->data,0,sizeof s->data);s->data_control&=~0x10u;
            /* Data success0x20 agrees with openiBoot SUCCESS_DATA.
             * Genuine Apple IRQ handler uses bit0 for command/DMA completion. */
            s->events|=0x20u;s->irq_pending|=1u;s->data_completions++;
        }
    }
    main_step(s);
    if (!s->busy || now < s->deadline) return;
    s->busy = false;
    s->command &= ~SDIO_EXECUTE;
    s->events |= SDIO_SENT;
    uint32_t response = 0;
    memset(s->response, 0, sizeof s->response);
    bool ok = descriptor_supported(s,s->pending_command) &&
              card_command(s, s->pending_command, s->pending_argument, &response);
    if (ok) {
        s->response[0] = response;
        s->events |= SDIO_DONE;
        s->completions++;
    } else {
        s->events |= SDIO_RESPONSE_ERROR;
        s->transport_errors++;
    }
    s->pending_command = s->pending_argument = 0;
    s->deadline = 0;
    /* +38's observed masks cover data/card interrupts, not a proven command
     * completion bit. Polling completion must not fabricate a VIC interrupt. */
}
bool sdio_irq_level(const sdio_t *s) {
    return (s->irq_pending & s->irq_mask) != 0;
}
uint32_t sdio_read(sdio_t *s, uint32_t offset, unsigned width, uint64_t now) {
    sdio_step(s, now);
    if (width != 4u || (offset & 3u) || offset >= SDIO_SIZE) return 0;
    if (offset >= SDIO_RESP0 && offset < SDIO_RESP0 + 16u) {
        s->response_reads++;
        return s->response[(offset - SDIO_RESP0) / 4u];
    }
    switch (offset) {
        case SDIO_CTRL: return s->control;
        case SDIO_DCTRL: return s->data_control;
        case SDIO_CMD: return s->command;
        case SDIO_ARG: return s->argument;
        case SDIO_STATE: return s->busy ? 0x10u : 0u;
        case SDIO_STATUS: return s->events |
            (((s->control & 1u) && !(s->data_control & 3u) && !s->busy)
                ? SDIO_READY : 0u);
        case SDIO_CLKDIV: return s->clock_divider;
        case SDIO_CSR: return s->csr;
        case SDIO_IRQ: return s->irq_pending;
        case SDIO_IRQMASK: return s->irq_mask;
        case SDIO_BUFFER:return s->buffer_address;
        case SDIO_BLOCK_LENGTH:return s->block_length;
        case SDIO_BLOCK_COUNT:return s->block_count;
        default: return 0;
    }
}
bool sdio_write(sdio_t *s, uint32_t offset, uint32_t value,
                unsigned width, uint64_t now) {
    sdio_step(s, now);
    if (width != 4u || (offset & 3u) || offset >= SDIO_SIZE) goto rejected;
    switch (offset) {
        case SDIO_CTRL:
            if (value & ~(s->config.enumeration ? 5u : 1u)) goto rejected;
            s->control = value;
            if (!(value & 1u)) { reset_data(s); abort_command(s); }
            return true;
        case SDIO_DCTRL:
            if(value==0x10u && s->config.data_transfer) {
                if(!start_data(s,now))goto rejected;
                s->data_control=value;return true;
            }
            if (value & ~3u) goto rejected;
            if(value&3u)reset_data(s);
            s->data_control = value;
            /* This is a FIFO reset, not a card or command-engine reset. */
            return true;
        case SDIO_ARG:
            if (s->busy) goto rejected;
            s->argument = value;
            return true;
        case SDIO_CMD:
            if (s->busy) goto rejected;
            if (!(value & SDIO_EXECUTE)) { s->command = value; return true; }
            if (!(s->control & 1u) || (s->data_control & 3u) ||
                UINT64_MAX - now < s->config.command_ticks) goto rejected;
            s->command = value;
            s->pending_command = value & ~SDIO_EXECUTE;
            s->pending_argument = s->argument;
            /* Starting another command is not acknowledgement of an old
             * event. Responses remain latched until the next completion. */
            s->deadline = now + s->config.command_ticks;
            s->busy = true;
            s->triggers++;
            return true;
        case SDIO_ACK:
            s->events &= ~value;
            return true;
        case SDIO_CLKDIV: s->clock_divider = value; return true;
        case SDIO_CSR: s->csr = value; return true;
        case SDIO_IRQ: s->irq_pending &= ~value; return true;
        case SDIO_IRQMASK: s->irq_mask = value; return true;
        case SDIO_BUFFER:if(s->busy||s->data.armed||s->data.active)goto rejected;s->buffer_address=value;return true;
        case SDIO_BLOCK_LENGTH:if(s->busy||s->data.armed||s->data.active)goto rejected;s->block_length=value;return true;
        case SDIO_BLOCK_COUNT:if(s->busy||s->data.armed||s->data.active)goto rejected;s->block_count=value;return true;
        default: break;
    }
rejected:
    s->rejected_writes++;
    return false;
}
