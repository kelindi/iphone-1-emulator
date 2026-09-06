/* Observe original PMU writes after one genuine navigation tap. No date,
 * callback result, CPU instruction or device register is injected. */
#include "emu_api.c"
#include <inttypes.h>
static bool (*underlying_write)(void*,uint8_t);
static unsigned rtc_writes;
static bool observe_write(void*ctx,uint8_t value) {
    s5l_pcf50635_t *pmu=ctx;
    bool date=pmu->have_ptr&&pmu->ptr>=0x59&&pmu->ptr<=0x5f;
    uint8_t reg=pmu->ptr;
    bool accepted=underlying_write(ctx,value);
    if(date) {
        rtc_writes++;
        printf("RTC WRITE ordinal%u register%02x value%02x accepted%u cycles%"PRIu64" ticks%"PRIu64" pc%08x unknownWrites%"PRIu64"\n",rtc_writes,reg,value,accepted,machine.cpu.cycles,machine.timer.ticks,machine.cpu.r[15],pmu->unknown_writes);
    }
    return accepted;
}
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    machine_ready=s5l_board_init(s5l_board_find("m68"),&machine,256u<<20);
    if(!machine_ready||snapshot_load(&machine,argv[1])!=SNAP_OK)return 3;
    machine.mtz1.uptime_ms=machine.timer.ticks/6000;
    machine.mtz1.uptime_tick_acc=machine.timer.ticks%6000;
    reset_host_inputs();emu_release_inputs();
    s5l_i2c_slave_t *slave=&machine.i2c[0].slaves[0];
    if(slave->addr!=PCF50635_I2C_ADDR||slave->ctx!=&machine.pmu)return 4;
    underlying_write=slave->write;slave->write=observe_write;
    uint64_t start=machine.cpu.cycles,start_unknown=machine.pmu.unknown_writes;
    printf("START cycles%"PRIu64" rtc%"PRIu64" unknownWrites%"PRIu64"\n",start,machine.pmu.seconds,start_unknown);
    if(emu_touch(1,35,42))return 5;
    bool released=false;
    for(unsigned i=0;i<100000000;i++) {
        if(!released&&machine.cpu.cycles-start>=5000000){if(emu_touch(0,35,42))return 6;released=true;}
        uint64_t before=machine.cpu.cycles;emu_run(1);
        if(machine.cpu.cycles!=before+1)return 7;
        if(rtc_writes>=7)break;
    }
    printf("END retired%"PRIu64" rtcWrites%u unknownDelta%"PRIu64" rtc%"PRIu64" pc%08x normalRunExit1\n",machine.cpu.cycles-start,rtc_writes,machine.pmu.unknown_writes-start_unknown,machine.pmu.seconds,machine.cpu.r[15]);
    release_machine();
    return rtc_writes==7?0:10;
}
