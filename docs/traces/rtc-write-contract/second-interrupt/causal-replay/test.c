#include "soc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* Separately compiled, unchanged production tick is the state oracle. */
void baseline_tick(s5l_pcf50635_t *, uint32_t);
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static uint8_t read_reg(s5l_pcf50635_t *p, uint8_t reg) {
    s5l_i2c_slave_t s; s5l_pcf50635_bind(p,&s);
    assert(s.start(s.ctx,false)); assert(s.write(s.ctx,reg));
    assert(s.start(s.ctx,true)); uint8_t v=s.read(s.ctx); s.stop(s.ctx);
    return v;
}
static void write_reg(s5l_pcf50635_t *p,uint8_t reg,uint8_t value) {
    s5l_i2c_slave_t s; s5l_pcf50635_bind(p,&s);
    assert(s.start(s.ctx,false)); assert(s.write(s.ctx,reg));
    assert(s.write(s.ctx,value)); s.stop(s.ctx);
}
int main(void) {
    s5l_pcf50635_t p,q,expected;
    s5l_pcf50635_reset(&p,6000000);
    CHECK(!s5l_pcf50635_irq(&p)); CHECK(p.regs[PCF50635_INT1]==0);
    write_reg(&p,PCF50635_INT1MASK,0x80);
    s5l_pcf50635_tick(&p,5999999);
    CHECK(p.seconds==PCF50635_DEFAULT_TIME); CHECK(p.regs[PCF50635_INT1]==0);
    s5l_pcf50635_tick(&p,1);
    CHECK(p.seconds==PCF50635_DEFAULT_TIME+1); CHECK(p.regs[PCF50635_INT1]==0x80);
    CHECK(!s5l_pcf50635_irq(&p));
    write_reg(&p,PCF50635_INT1MASK,0);
    CHECK(s5l_pcf50635_irq(&p)); CHECK(read_reg(&p,PCF50635_INT1)==0x80);
    CHECK(!s5l_pcf50635_irq(&p)); CHECK(read_reg(&p,PCF50635_INT1)==0);
    s5l_pcf50635_tick(&p,5999999); CHECK(!s5l_pcf50635_irq(&p));
    s5l_pcf50635_tick(&p,1); CHECK(s5l_pcf50635_irq(&p));
    s5l_pcf50635_tick(&p,18000000); CHECK(read_reg(&p,PCF50635_INT1)==0x80);
    CHECK(p.seconds==PCF50635_DEFAULT_TIME+5);

    /* Compare every state byte against old behavior, allowing exactly the
     * new event latch when the bounded calendar advances. Other PMU event,
     * mask, unknown-register and transaction state must survive unchanged. */
    const uint32_t rates[]={0,1,3,6000000,UINT32_MAX};
    const uint32_t steps[]={0,1,2,5999999,6000000,6000001,UINT32_MAX};
    const uint64_t times[]={PCF50635_MIN_TIME,PCF50635_DEFAULT_TIME,
        PCF50635_MAX_TIME-1,PCF50635_MAX_TIME};
    for(unsigned h=0;h<5;h++) for(unsigned s=0;s<7;s++)
    for(unsigned t=0;t<4;t++) for(unsigned r=0;r<3;r++) {
        s5l_pcf50635_reset(&p,rates[h]); p.seconds=times[t];
        p.tick_accum=r==0?0:r==1?(rates[h]?rates[h]-1:0):UINT64_MAX;
        for(unsigned i=0;i<PCF50635_NREG;i++) {
            p.regs[i]=(uint8_t)(i*17+3); p.written[i]=(uint8_t)(i&1);
        }
        p.regs[PCF50635_INT1]&=0x7f;
        memcpy(&expected,&p,sizeof p);
        baseline_tick(&expected,steps[s]);
        if(expected.seconds!=p.seconds) expected.regs[PCF50635_INT1]|=0x80;
        s5l_pcf50635_tick(&p,steps[s]);
        CHECK(!memcmp(&p,&expected,sizeof p));
    }
    /* Calendar saturation remains old policy, not an oscillator claim. */
    s5l_pcf50635_reset(&p,6000000); p.seconds=PCF50635_MAX_TIME;
    s5l_pcf50635_tick(&p,6000000); CHECK(!s5l_pcf50635_irq(&p));
    CHECK(p.tick_accum==0); CHECK(p.seconds==PCF50635_MAX_TIME);

    /* No intermediate reads: split and lumped advancement must agree. */
    s5l_pcf50635_reset(&p,6000000); memcpy(&q,&p,sizeof p);
    for(unsigned i=0;i<103;i++) s5l_pcf50635_tick(&p,123456);
    s5l_pcf50635_tick(&q,103*123456); CHECK(!memcmp(&p,&q,sizeof p));
    /* Explicit time-setting/reset must not synthesize an elapsed edge. */
    s5l_pcf50635_reset(&p,6000000);
    s5l_pcf50635_set_time(&p,PCF50635_DEFAULT_TIME+1);
    CHECK(!s5l_pcf50635_irq(&p)); CHECK(p.regs[PCF50635_INT1]==0);
    s5l_pcf50635_tick(NULL,1);
    printf("PASS %u SECOND-edge/state contracts\n",checks); return 0;
}
