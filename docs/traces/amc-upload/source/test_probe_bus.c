#define main upload_probe_main
#include "probe.c"
#undef main
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"bus line%u: %s\n",__LINE__,#x); exit(1); } } while(0)
static amc_sram_t before;
static void reject_sram(uint32_t a,unsigned width,uint32_t value) {
    before=sram;unsigned old_writes=sramW;
    result=0;
    if(!setjmp(finish)) { writebus(&machine,a,width,value); CHECK(false); }
    CHECK(result==1);CHECK(!memcmp(&before,&sram,sizeof sram));CHECK(sramW==old_writes);
}
int main(void) {
    amc_sram_init(&sram);amc_idle_init(&idle);idle.upload=true;
    reject_sram(AMC_SRAM_BASE,4,0);
    reject_sram(AMC_SRAM_BASE+0x28000,4,1);
    /* The same zero value as initial SRAM must still be refused if undeclared. */
    reject_sram(AMC_SRAM_BASE+0x14000,4,0);
    prediction(AMC_SRAM_BASE+0x28002,2,0x1234);
    writebus(&machine,AMC_SRAM_BASE+0x28000,4,0x12340000);
    CHECK(sramW==1);CHECK(!memcmp(&sram,&expected,sizeof sram));
    CHECK(written[0x28000]&&written[0x28001]&&written[0x28002]&&written[0x28003]);
    reject_sram(AMC_SRAM_BASE+0x28004,4,0);
    reject_sram(AMC_SRAM_BASE+0x28000,2,0);
    reject_sram(AMC_SRAM_BASE+0x28001,4,0);
    reject_sram(AMC_SRAM_BASE+AMC_SRAM_SIZE-1,4,0);
    reject_sram(AMC_SRAM_BASE-1,4,0);
    idle.upload=false;
    reject_sram(AMC_SRAM_BASE+0x28000,4,0);
    printf("isolated upload footprint: %u checks passed\n",checks);
    return 0;
}
