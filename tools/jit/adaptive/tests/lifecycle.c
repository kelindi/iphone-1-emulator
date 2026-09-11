/* Real snapshot application against the optional ABI2 lifetime barrier.
 * Uses a native guarded callback, not generated WASM or a guest workload. */
#include "soc.h"
#include "snapshot.h"
#include "adaptive-runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, calls;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#x); exit(1); \
} } while (0)
static s5l8900_t machine;
static uint8_t *saved;
static size_t saved_size;
static snapshot_status_t attempted_restore;

static void guarded(arm_cpu_t *cpu, uint32_t budget,
                    arm_adaptive_result_t *result) {
    (void)budget; ++calls;
    CHECK(adaptive_guard(cpu->r[15],0xe1a00000u,4));
    *result=(arm_adaptive_result_t){0,ADAPT_EXIT_BEFORE,ARM_OK,
        cpu->r[15],cpu->r[15],UINT32_MAX,0,0};
}

static void restoring(arm_cpu_t *cpu, uint32_t budget,
                      arm_adaptive_result_t *result) {
    guarded(cpu,budget,result);
    attempted_restore=snapshot_load_mem(&machine,saved,saved_size);
    CHECK(attempted_restore == SNAP_ERR_BUSY);
}

static void freeing(arm_cpu_t *cpu, uint32_t budget,
                     arm_adaptive_result_t *result) {
    guarded(cpu,budget,result);
    uint8_t *ram=machine.ram, *edram=machine.mbx.edram;
    s5l8900_free(&machine);
    CHECK(machine.ram==ram && machine.mbx.edram==edram);
    CHECK(adaptive_failed());
}

static void assert_snapshot(const uint8_t *bytes, size_t size) {
    uint8_t *actual=NULL; size_t actual_size=0;
    CHECK(snapshot_save_mem(&machine,&actual,&actual_size)==SNAP_OK);
    CHECK(actual_size==size);
    CHECK(!memcmp(actual,bytes,size));
    free(actual);
}

static void setup(void) {
    adaptive_bind(NULL);
    CHECK(!adaptive_failed());
    arm_adaptive_fn retired[ADAPTIVE_UNIT_CAP];
    (void)adaptive_retired(retired,ADAPTIVE_UNIT_CAP);
    CHECK(s5l8900_init(&machine,0x08000000u,0x200000u));
    const uint32_t nop=0xe1a00000u;
    s5l8900_load(&machine,machine.ram_base,&nop,4);
    machine.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
    machine.cpu.r[15]=machine.ram_base;
    CHECK(arm_step(&machine.cpu)==ARM_OK);
    machine.cpu.r[15]=machine.ram_base;
    CHECK(snapshot_save_mem(&machine,&saved,&saved_size)==SNAP_OK);
    CHECK(saved && saved_size>8);
    calls=0;
}

static uint32_t install(arm_adaptive_fn fn) {
    adaptive_bind(&machine.cpu);
    CHECK(!adaptive_failed());
    CHECK(adaptive_track_ram(machine.ram,machine.ram_size));
    machine.bus.ram_written=adaptive_ram_written;
    arm_adaptive_fetch_witness_t witness;
    CHECK(adaptive_capture(&witness));
    CHECK(witness.code_version==1);
    CHECK(adaptive_install(machine.cpu.r[15],0,fn,&witness,1,
                           adaptive_epoch(),64));
    adaptive_enable(1);
    CHECK(arm_adaptive_enabled && adaptive_units()==1);
    return adaptive_epoch();
}

static void retired_once(arm_adaptive_fn fn) {
    arm_adaptive_fn out[2]={NULL,NULL};
    CHECK(adaptive_retired(out,2)==1);
    CHECK(out[0]==fn);
    CHECK(adaptive_retired(out,2)==0);
}

static void cleanup(void) {
    adaptive_bind(NULL);
    CHECK(!adaptive_failed());
    arm_adaptive_fn out[ADAPTIVE_UNIT_CAP];
    (void)adaptive_retired(out,ADAPTIVE_UNIT_CAP);
    s5l8900_free(&machine);
    free(saved); saved=NULL; saved_size=0;
}

static void successful_restore(void) {
    setup(); uint32_t epoch=install(guarded);
    machine.bus.write32(machine.bus.ctx,machine.ram_base+0x800u,0x11223344u);
    CHECK(adaptive_code_version(machine.ram+0x800u)==2);
    CHECK(snapshot_load_mem(&machine,saved,saved_size)==SNAP_OK);
    CHECK(adaptive_epoch()!=epoch);
    CHECK(!adaptive_units() && !arm_adaptive_enabled && !adaptive_failed());
    CHECK(adaptive_code_version(machine.ram)==0);
    CHECK(adaptive_code_version(machine.ram+0x800u)==0);
    retired_once(guarded);
    assert_snapshot(saved,saved_size);
    arm_adaptive_result_t result;
    CHECK(arm_adaptive_try(&machine.cpu,1,&result)==0 && calls==0);
    cleanup();
}

static void corrupt_restore(void) {
    setup(); uint32_t epoch=install(guarded);
    uint8_t *corrupt=malloc(saved_size); CHECK(corrupt);
    memcpy(corrupt,saved,saved_size); corrupt[saved_size-1]^=0x80;
    CHECK(snapshot_load_mem(&machine,corrupt,saved_size)==SNAP_ERR_CHECKSUM);
    CHECK(adaptive_epoch()==epoch);
    CHECK(adaptive_units()==1 && arm_adaptive_enabled && !adaptive_failed());
    CHECK(adaptive_code_version(machine.ram)==1);
    arm_adaptive_fn out;
    CHECK(adaptive_retired(&out,1)==0);
    assert_snapshot(saved,saved_size);
    arm_adaptive_result_t result;
    CHECK(arm_adaptive_try(&machine.cpu,1,&result)==1 && calls==1);
    CHECK(result.retired==0 && result.reason==ADAPT_EXIT_BEFORE);
    free(corrupt); cleanup();
}

static void active_restore(void) {
    setup(); uint32_t epoch=install(restoring);
    machine.bus.write32(machine.bus.ctx,machine.ram_base+0x800u,0x89abcdefu);
    uint8_t *before=NULL; size_t before_size=0;
    CHECK(snapshot_save_mem(&machine,&before,&before_size)==SNAP_OK);
    uint64_t cycles=machine.cpu.cycles;
    arm_adaptive_result_t result;
    CHECK(arm_adaptive_try(&machine.cpu,1,&result)==-1);
    CHECK(calls==1 && adaptive_failed() && arm_adaptive_enabled);
    CHECK(adaptive_epoch()==epoch && adaptive_units()==1);
    CHECK(adaptive_code_version(machine.ram+0x800u)==2);
    CHECK(machine.cpu.cycles==cycles);
    assert_snapshot(before,before_size);
    CHECK(arm_adaptive_try(&machine.cpu,1,&result)==-1 && calls==1);
    arm_status_t status=ARM_OK;
    CHECK(s5l8900_run(&machine,1,&status)==0 && status==ARM_HALT);
    CHECK(machine.cpu.cycles==cycles && calls==1);
    assert_snapshot(before,before_size);
    adaptive_bind(NULL); CHECK(!adaptive_failed());
    retired_once(restoring);
    free(before); cleanup();
}

static void disable_and_unrelated(void) {
    setup(); uint32_t epoch=install(guarded);
    arm_cpu_t other={0};
    CHECK(arm_adaptive_forget(&other)==1);
    CHECK(adaptive_epoch()==epoch && adaptive_units()==1);
    CHECK(adaptive_code_version(machine.ram)==1);
    adaptive_enable(0);
    CHECK(adaptive_epoch()!=epoch && !adaptive_units());
    CHECK(!arm_adaptive_enabled && !adaptive_failed());
    CHECK(adaptive_code_version(machine.ram)==0);
    retired_once(guarded);
    cleanup();
}

static void inactive_free(void) {
    setup(); uint32_t epoch=install(guarded);
    s5l8900_free(&machine);
    CHECK(machine.ram==NULL && machine.mbx.edram==NULL);
    CHECK(adaptive_epoch()!=epoch);
    CHECK(!adaptive_units() && !arm_adaptive_enabled && !adaptive_failed());
    arm_adaptive_fetch_witness_t witness;
    CHECK(!adaptive_capture(&witness)); /* No retained bound CPU/fetch pointer. */
    retired_once(guarded);
    arm_adaptive_result_t result;
    CHECK(arm_adaptive_try(&machine.cpu,1,&result)==0 && calls==0);
    free(saved); saved=NULL; saved_size=0;
}

static void capture_only_disable(void) {
    setup(); adaptive_bind(&machine.cpu);
    CHECK(!adaptive_failed() && !arm_adaptive_enabled && !adaptive_units());
    arm_adaptive_fetch_witness_t witness;
    CHECK(adaptive_capture(&witness));
    CHECK(witness.code_version==0);
    uint32_t epoch=adaptive_epoch();
    adaptive_enable(0);
    CHECK(adaptive_epoch()!=epoch);
    CHECK(!adaptive_failed() && !arm_adaptive_enabled && !adaptive_units());
    CHECK(!adaptive_install(machine.cpu.r[15],0,guarded,&witness,1,epoch,64));
    /* Updating only the request epoch must not rehabilitate its old witness. */
    CHECK(!adaptive_install(machine.cpu.r[15],0,guarded,&witness,1,
                            adaptive_epoch(),64));
    CHECK(adaptive_capture(&witness));
    CHECK(adaptive_install(machine.cpu.r[15],0,guarded,&witness,1,
                           adaptive_epoch(),64));
    cleanup();
}

static void active_free(void) {
    setup(); uint32_t epoch=install(freeing);
    machine.bus.write32(machine.bus.ctx,machine.ram_base+0x800u,0x12345678u);
    uint8_t *before=NULL; size_t before_size=0;
    CHECK(snapshot_save_mem(&machine,&before,&before_size)==SNAP_OK);
    uint8_t *ram=machine.ram, *edram=machine.mbx.edram;
    uint64_t cycles=machine.cpu.cycles;
    arm_adaptive_result_t result;
    CHECK(arm_adaptive_try(&machine.cpu,1,&result)==-1);
    CHECK(machine.ram==ram && machine.mbx.edram==edram);
    CHECK(calls==1 && adaptive_failed() && arm_adaptive_enabled);
    CHECK(adaptive_epoch()==epoch && adaptive_units()==1);
    CHECK(adaptive_code_version(machine.ram+0x800u)==2);
    CHECK(machine.cpu.cycles==cycles);
    assert_snapshot(before,before_size); /* Includes RAM, NOR and device state. */
    CHECK(arm_adaptive_try(&machine.cpu,1,&result)==-1 && calls==1);
    arm_status_t status=ARM_OK;
    CHECK(s5l8900_run(&machine,1,&status)==0 && status==ARM_HALT);
    CHECK(machine.cpu.cycles==cycles && calls==1);
    assert_snapshot(before,before_size);
    adaptive_bind(NULL); CHECK(!adaptive_failed());
    retired_once(freeing);
    free(before); cleanup();
}

int main(void) {
    successful_restore(); corrupt_restore(); active_restore();
    disable_and_unrelated(); inactive_free(); active_free();
    capture_only_disable();
    printf("adaptive lifecycle: %u checks PASS (7 cases)\n",checks);
    return 0;
}
