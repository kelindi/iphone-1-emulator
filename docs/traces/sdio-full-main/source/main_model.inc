/* Source: ../helper-protocol/README.md. Semantic model only; no helper CPU. */
#include "supported_helper.inc"
static void main_reset(sdio_t *s) {
    memset(&s->main_model,0,sizeof s->main_model);
    if(s->config.complete_main){
        memset(s->main_model.low_before,0xa5,16);memset(s->main_model.low_after,0xa5,16);
        memset(s->main_model.high_before,0xa5,16);memset(s->main_model.high_after,0xa5,16);
    }
}
static bool main_pending(const sdio_t *s) {
    return s->main_model.phase==MAIN_BOOT_PENDING || s->main_model.phase==MAIN_HEADER_PENDING ||
           s->main_model.phase==MAIN_BODY_PENDING;
}
static void main_reanchor(sdio_t *s) {
    if(main_pending(s)){s->main_model.deadline=0;s->main_model.waiting_deadline=true;}
}
static bool main_available(const sdio_t *s) {
    return s->config.present && s->card.state==CARD_SELECTED &&
        s->endpoint.initialized && s->endpoint.enabled && s->endpoint.ready &&
        s->endpoint.host_power && s->endpoint.capacity>=512;
}
static unsigned main_wire(const sdio_t *s) {
    return ((unsigned)s->main_model.requested+31u)&~31u;
}
static bool main_room(const sdio_t *s,size_t length) {
    return s->config.helper_semantics &&
        (s->main_model.phase==MAIN_HEADER || s->main_model.phase==MAIN_BODY) &&
        s->endpoint.used==0 && !s->main_model.pending_length &&
        length==main_wire(s) && (length==32 || length==512 ||
         (s->config.complete_main&&length>=32&&length<=512&&length%32==0));
}
static void main_stop(sdio_t *s,unsigned diagnostic) {
    s->main_model.errors++;s->main_model.diagnostic=diagnostic;s->main_model.phase=MAIN_STOPPED;
    s->main_model.deadline=0;s->main_model.waiting_deadline=false;
}
static void main_publish(sdio_t *s,uint16_t length,sdio_main_phase_t phase) {
    s->main_model.requested=length;s->main_model.phase=phase;s->main_model.publications++;
    s->main_model.deadline=0;s->main_model.waiting_deadline=false;
}
static void main_schedule(sdio_t *s) {
    s->main_model.waiting_deadline=true;s->main_model.deadline=0;
    if(main_available(s)) {
        if(UINT64_MAX-s->now<s->config.semantic_ticks){main_stop(s,3);return;}
        s->main_model.deadline=s->now+s->config.semantic_ticks;
        s->main_model.waiting_deadline=false;
    }
}
static void main_admit(sdio_t *s) {
    if(s->config.helper_semantics && s->main_model.phase==MAIN_OFF &&
       s->helper.terminated && !s->helper.framing_stalled &&
       s->helper.used==sizeof supported_helper &&
       !memcmp(s->helper.memory,supported_helper,sizeof supported_helper)) {
        s->main_model.phase=MAIN_BOOT_PENDING;main_schedule(s);
    }
}
static void main_receive(sdio_t *s,const uint8_t *frame,size_t length) {
    /* Entire frame already arrived physically. Keep it outside the endpoint
     * and host transfer structures so a reset/rebind cannot discard it. */
    if(s->endpoint.used!=length || length>sizeof s->main_model.pending ||
       s->main_model.pending_length ||
       (s->main_model.phase!=MAIN_HEADER && s->main_model.phase!=MAIN_BODY)) {
        main_stop(s,1);return;
    }
    memcpy(s->main_model.pending,frame,length);s->main_model.pending_length=(uint16_t)length;
    s->main_model.phase=s->main_model.phase==MAIN_HEADER ? MAIN_HEADER_PENDING:MAIN_BODY_PENDING;
    s->endpoint.used=0;memset(s->endpoint.receive,0,sizeof s->endpoint.receive);
    main_schedule(s);
}
static uint32_t main_word(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static bool main_bounds(const sdio_t *s,const uint32_t header[4]) {
    if(s->config.complete_main){
        if(header[2]<4u||header[2]>512u)return false;
        uint32_t n=header[2]-4u,address=header[1];
        return (address<=0x17000u&&n<=0x17000u-address) ||
            (address>=0xc0001000u&&address-0xc0001000u<=0x6000u&&
             n<=0x6000u-(address-0xc0001000u));
    }
    return header[2]==512 && header[1]>=0xc0001000u &&
        header[1]-0xc0001000u<=512u &&
        header[2]-4u<=512u-(header[1]-0xc0001000u);
}
static void main_step(sdio_t *s) {
    if(!main_pending(s))return;
    if(!main_available(s)){main_reanchor(s);return;}
    if(s->main_model.waiting_deadline){main_schedule(s);return;}
    if(s->now<s->main_model.deadline)return;
    sdio_main_t *m=&s->main_model;
    if(m->phase==MAIN_BOOT_PENDING){main_publish(s,16,MAIN_HEADER);return;}
    if(m->phase==MAIN_HEADER_PENDING) {
        if(m->pending_length!=32){main_stop(s,1);return;}
        uint32_t header[4];for(unsigned i=0;i<4;i++)header[i]=main_word(m->pending+4*i);
        if(header[0]==4){
            if(s->config.complete_main){
                memcpy(m->header,header,sizeof header);m->headers++;
                main_publish(s,0,MAIN_HANDOFF); /* record request, never branch */
            } else {m->requested=0;main_stop(s,2);}
            return;
        }
        if(header[0]==1 && !main_bounds(s,header)){main_stop(s,1);return;}
        memcpy(m->header,header,sizeof header);m->headers++;
        m->pending_length=0;memset(m->pending,0,sizeof m->pending);
        if(header[0]==1)main_publish(s,(uint16_t)header[2],MAIN_BODY);
        else main_publish(s,17,MAIN_HEADER); /* original requested-length error indication;
                                              Apple host aborts on the odd value */
        return;
    }
    if(m->pending_length!=((m->header[2]+31u)&~31u) || !main_bounds(s,m->header) || (!s->config.complete_main&&m->bodies)) {
        main_stop(s,1);return;
    }
    unsigned copied=m->header[2]-4u;
    if(s->config.complete_main){
        uint32_t address=m->header[1];
        uint8_t *memory=address<0xc0001000u?m->low+address:m->high+(address-0xc0001000u);
        uint8_t *coverage=address<0xc0001000u?m->low_coverage+address:m->high_coverage+(address-0xc0001000u);
        memcpy(memory,m->pending,copied);memset(coverage,1,copied);
    } else memcpy(m->memory+(m->header[1]-0xc0001000u),m->pending,copied);
    m->bodies++;m->copied+=copied;
    m->pending_length=0;memset(m->pending,0,sizeof m->pending);
    memset(m->header,0,sizeof m->header);
    main_publish(s,16,MAIN_HEADER);
}
