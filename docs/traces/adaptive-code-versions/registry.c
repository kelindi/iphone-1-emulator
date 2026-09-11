#include "adaptive-runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "registry:%d: %s\n", __LINE__, #expr); exit(1); \
} } while (0)
static uint8_t ram[9u * 1024u];
static arm_cpu_t cpu;
static arm_adaptive_fetch_witness_t witness;
static unsigned reads, calls, mutation;
static arm_adaptive_fn removed[ADAPTIVE_RETIRED_CAP];
static uint8_t r8(void *ctx, uint32_t address) {
    (void)ctx; CHECK(address < sizeof ram); reads++; return ram[address];
}
static uint16_t r16(void *ctx, uint32_t address) {
    (void)ctx; CHECK(address <= sizeof ram - 2); reads++;
    return (uint16_t)(ram[address] | (uint16_t)ram[address+1] << 8);
}
static uint32_t r32(void *ctx, uint32_t address) {
    (void)ctx; CHECK(address <= sizeof ram - 4); reads++;
    return (uint32_t)ram[address] | (uint32_t)ram[address+1] << 8 |
        (uint32_t)ram[address+2] << 16 | (uint32_t)ram[address+3] << 24;
}
static uint8_t *host(void *ctx, uint32_t address, uint32_t length) {
    (void)ctx;
    return (uint64_t)address + length <= sizeof ram ? ram + address : NULL;
}
static const arm_bus_t bus = {
    .read8=r8, .read16=r16, .read32=r32, .host_ram=host
};
static unsigned install(arm_adaptive_fn fn, unsigned bytes) {
    return adaptive_install(0, 0, fn, &witness, 1, adaptive_epoch(), bytes);
}
static void finish(arm_cpu_t *c, uint32_t budget, arm_adaptive_result_t *result) {
    calls++;
    c->r[0] += budget;
    c->r[15] += budget * 4;
    *result = (arm_adaptive_result_t){budget, ADAPT_EXIT_BUDGET, ARM_OK,
        c->r[15], c->r[15], 0, 0, 0};
}
static void finish_other(arm_cpu_t *c, uint32_t budget, arm_adaptive_result_t *result) {
    finish(c, budget, result);
}
static void stale(arm_cpu_t *c, uint32_t budget, arm_adaptive_result_t *result) {
    (void)budget; calls++;
    CHECK(!adaptive_guard(c->r[15], 0xe1a00001u, 4));
    *result = (arm_adaptive_result_t){0, ADAPT_EXIT_STALE, ARM_OK,
        c->r[15], c->r[15], 0, 0, 0};
}
static void checked_guard(arm_cpu_t *c, uint32_t budget, arm_adaptive_result_t *result) {
    arm_cpu_t before = *c;
    unsigned old_reads = reads;
    CHECK(adaptive_guard(c->r[15], 0xe1a00000u, 4));
    CHECK(!adaptive_guard(c->r[15], 0xe1a00001u, 4));
    CHECK(!adaptive_guard(1024, 0xe1a00000u, 4));
    CHECK(!memcmp(c, &before, sizeof before)); CHECK(reads == old_reads);
    finish(c, budget, result);
}
static void mutate(arm_cpu_t *c, uint32_t budget, arm_adaptive_result_t *result) {
    arm_adaptive_fetch_witness_t out;
    arm_adaptive_fn handle;
    arm_adaptive_result_t nested;
    finish(c, budget, result); /* visible callee effects cannot be rolled back */
    switch (mutation) {
    case 0: adaptive_bind(NULL); break;
    case 1: CHECK(!install(finish_other, 64)); break;
    case 2: CHECK(!adaptive_retired(&handle, 1)); break;
    case 3: adaptive_enable(0); break;
    case 4: CHECK(!adaptive_capture(&out)); break;
    case 5: CHECK(arm_adaptive_try(c, 1, &nested) == -1); break;
    }
}
static void written_while_active(arm_cpu_t *c, uint32_t budget,
                                  arm_adaptive_result_t *result) {
    (void)budget;
    calls++;
    adaptive_ram_written(c->bus->ctx, ram, 4);
    CHECK(!adaptive_failed());
    CHECK(!adaptive_guard(0, 0xe1a00000u, 4));
    *result=(arm_adaptive_result_t){0,ADAPT_EXIT_STALE,ARM_OK,0,0,0,0,0};
}
static void reset(void) {
    adaptive_bind(NULL);
    CHECK(!adaptive_failed()); CHECK(!arm_adaptive_enabled);
    (void)adaptive_retired(removed, ADAPTIVE_RETIRED_CAP);
    memset(&cpu, 0, sizeof cpu); memset(ram, 0, sizeof ram);
    ram[0]=0; ram[1]=0; ram[2]=0xa0; ram[3]=0xe1; /* MOV r0,r0 */
    arm_reset(&cpu, &bus);
    CHECK(cpu.arch == ARM_ARCH_V6_ARM1176);
    cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    CHECK(arm_step(&cpu) == ARM_OK); CHECK(cpu.fetch_host == ram);
    cpu.r[15]=0;
    adaptive_bind(&cpu);
    CHECK(adaptive_epoch() != 0); CHECK(!adaptive_failed());
    CHECK(adaptive_capture(&witness));
}
/* Opaque unique handles are used ONLY as registry data. They are never invoked.
 * This conversion is host-specific test scaffolding, not a portable code ABI. */
static arm_adaptive_fn opaque(unsigned n) {
    return (arm_adaptive_fn)(uintptr_t)(0x10000000u + 16u*n);
}
int main(void) {
    arm_adaptive_result_t result;
    reset();
    CHECK(!adaptive_guard(0, 0xe1a00000u, 4));
    CHECK(install(checked_guard, 64)); adaptive_enable(1);
    uint64_t cycles=cpu.cycles;
    CHECK(arm_adaptive_try(&cpu, 2, &result) == 1);
    CHECK(cpu.cycles == cycles+2 && result.retired == 2);
    CHECK(!adaptive_guard(0, 0xe1a00000u, 4));

    reset();
    CHECK(install(finish, 64)); CHECK(!install(finish, 64));
    CHECK(!adaptive_install(4, 0, finish, &witness, 1, adaptive_epoch(), 64));
    CHECK(install(finish_other, 80)); CHECK(adaptive_units() == 1);
    CHECK(!install(finish, 64)); /* pending retirement still owns finish */
    CHECK(adaptive_retired(removed, 1) == 1 && removed[0] == finish);
    CHECK(install(finish, 64));
    CHECK(adaptive_retired(removed, 1) == 1 && removed[0] == finish_other);
    CHECK(!adaptive_retired(removed, 1));

    arm_adaptive_fetch_witness_t old=witness;
    uint32_t old_epoch=adaptive_epoch();
    adaptive_bind(&cpu); CHECK(adaptive_epoch() != old_epoch);
    CHECK(!adaptive_install(0, 0, finish_other, &old, 1, old_epoch, 64));
    CHECK(!adaptive_install(0, 0, finish_other, &old, 1, adaptive_epoch(), 64));
    CHECK(adaptive_retired(removed, ADAPTIVE_RETIRED_CAP) == 1);
    CHECK(adaptive_capture(&witness));
    arm_adaptive_fetch_witness_t invalid=witness;
    invalid.host=(const uint8_t *)(uintptr_t)1; invalid.epoch=old_epoch;
    CHECK(!adaptive_install(0, 0, finish, &invalid, 1, adaptive_epoch(), 64));
    invalid=witness; invalid.host=ram+1024;
    CHECK(!adaptive_install(0, 0, finish, &invalid, 1, adaptive_epoch(), 64));
    CHECK(!adaptive_install(0, 0, finish, &witness, 0, adaptive_epoch(), 64));
    CHECK(!adaptive_install(0, 0, finish, &witness, 9, adaptive_epoch(), 64));

    reset();
    CHECK(!install(finish, 0)); CHECK(!install(finish, ADAPTIVE_BINARY_CAP+1));
    CHECK(install(finish, ADAPTIVE_BINARY_CAP));
    CHECK(!adaptive_install(4, 0, finish_other, &witness, 1, adaptive_epoch(), 1));
    CHECK(install(finish_other, 1)); /* replacement frees old binary allowance */
    CHECK(adaptive_retired(removed, 1) == 1 && removed[0] == finish);
    CHECK(adaptive_install(4, 0, finish, &witness, 1, adaptive_epoch(), ADAPTIVE_BINARY_CAP-1));
    CHECK(adaptive_units() == 2);

    reset();
    CHECK(install(stale, 64)); adaptive_enable(1); cycles=cpu.cycles;
    CHECK(arm_adaptive_try(&cpu, 2, &result) == 1);
    CHECK(result.reason == ADAPT_EXIT_STALE && !result.retired);
    CHECK(cpu.cycles == cycles && adaptive_units() == 0);
    CHECK(adaptive_retired(removed, 1) == 1 && removed[0] == stale);
    CHECK(arm_adaptive_try(&cpu, 2, &result) == 0);

    for(mutation=0; mutation<6; mutation++) {
        reset(); CHECK(install(mutate, 64)); adaptive_enable(1);
        cycles=cpu.cycles; unsigned initial=calls;
        CHECK(arm_adaptive_try(&cpu, 2, &result) == -1);
        CHECK(adaptive_failed() && arm_adaptive_enabled);
        CHECK(cpu.cycles == cycles && cpu.r[0] == 2 && cpu.r[15] == 8);
        CHECK(calls == initial+1 && adaptive_units() == 1);
        arm_cpu_t after=cpu;
        adaptive_enable(0); CHECK(arm_adaptive_enabled);
        CHECK(arm_adaptive_try(&cpu, 2, &result) == -1);
        CHECK(arm_adaptive_try(&cpu, 0, &result) == -1);
        CHECK(!memcmp(&after, &cpu, sizeof cpu) && calls == initial+1);
        adaptive_bind(&cpu); CHECK(!adaptive_failed() && !arm_adaptive_enabled);
        CHECK(adaptive_retired(removed, 1) == 1 && removed[0] == mutate);
        CHECK(adaptive_units() == 0); cpu.r[15]=0;
        CHECK(adaptive_capture(&witness)); CHECK(install(finish, 64));
        adaptive_enable(1); CHECK(arm_adaptive_try(&cpu, 1, &result) == 1);
        CHECK(cpu.cycles == cycles+1);
    }

    reset();
    for(unsigned i=0; i<ADAPTIVE_RETIRED_CAP; i++) CHECK(install(opaque(i), 1));
    CHECK(install(finish, 1)); /* now exactly CAP retired opaque handles */
    CHECK(adaptive_units() == 1 && !adaptive_failed());
    CHECK(!install(finish_other, 1));
    CHECK(!adaptive_install(4, 0, opaque(0), &witness, 1, adaptive_epoch(), 1));
    adaptive_enable(1); unsigned initial=calls; cycles=cpu.cycles;
    CHECK(arm_adaptive_try(&cpu, 2, &result) == 0);
    CHECK(calls == initial && cpu.cycles == cycles); /* reference fallback only */
    old_epoch=adaptive_epoch(); adaptive_bind(&cpu);
    CHECK(adaptive_failed() && arm_adaptive_enabled && adaptive_units() == 1);
    CHECK(adaptive_epoch() != old_epoch);
    adaptive_enable(0); CHECK(arm_adaptive_enabled);
    CHECK(arm_adaptive_try(&cpu, 1, &result) == -1);
    CHECK(adaptive_retired(removed, 17) == 17);
    for(unsigned i=0;i<17;i++) CHECK(removed[i] == opaque(i));
    adaptive_bind(&cpu); CHECK(!adaptive_failed() && !arm_adaptive_enabled);
    CHECK(adaptive_units() == 0);
    CHECK(adaptive_retired(removed, ADAPTIVE_RETIRED_CAP) == ADAPTIVE_RETIRED_CAP-17+1);
    for(unsigned i=17;i<ADAPTIVE_RETIRED_CAP;i++) CHECK(removed[i-17] == opaque(i));
    CHECK(removed[ADAPTIVE_RETIRED_CAP-17] == finish);
    CHECK(!adaptive_retired(removed, ADAPTIVE_RETIRED_CAP));
    CHECK(adaptive_capture(&witness)); CHECK(install(finish_other, 1));

    /* Full unit cap with genuinely warmed captures across eight windows.
     * Opaque handles remain non-executable registry fixtures. */
    reset();
    for(unsigned block=0;block<8;block++) {
        unsigned base=block*1024;
        ram[base]=0; ram[base+1]=0; ram[base+2]=0xa0; ram[base+3]=0xe1;
        cpu.r[15]=base; CHECK(arm_step(&cpu) == ARM_OK);
        CHECK(adaptive_capture(&witness));
        for(unsigned offset=0;offset<256;offset++) {
            unsigned i=block*256+offset;
            CHECK(adaptive_install(base+offset*4, 0, opaque(i), &witness,
                                   1, adaptive_epoch(), 1));
        }
    }
    CHECK(adaptive_units() == ADAPTIVE_UNIT_CAP);
    cpu.r[15]=8*1024;
    CHECK(arm_step(&cpu) == ARM_OK); CHECK(adaptive_capture(&witness));
    CHECK(!adaptive_install(8*1024, 0, finish, &witness, 1, adaptive_epoch(), 1));
    CHECK(adaptive_units() == ADAPTIVE_UNIT_CAP);
    adaptive_bind(NULL); CHECK(!adaptive_failed());
    CHECK(adaptive_retired(removed, ADAPTIVE_RETIRED_CAP) == ADAPTIVE_UNIT_CAP);
    for(unsigned i=0;i<ADAPTIVE_UNIT_CAP;i++) CHECK(removed[i] == opaque(i));
    reset();
    CHECK(adaptive_track_ram(ram,sizeof ram));
    CHECK(!adaptive_track_ram(ram,sizeof ram));
    CHECK(adaptive_capture(&witness) && witness.code_version==1);
    CHECK(install(written_while_active,64));adaptive_enable(1);
    cycles=cpu.cycles;
    CHECK(arm_adaptive_try(&cpu,4,&result)==1);
    CHECK(result.reason==ADAPT_EXIT_STALE && !result.retired);
    CHECK(cpu.cycles==cycles && adaptive_code_version(ram)==2);
    CHECK(adaptive_retired(removed,1)==1 && removed[0]==written_while_active);
    CHECK(!install(finish,64)); /* pre-write captured work is still stale */
    CHECK(adaptive_capture(&witness) && witness.code_version==2);
    CHECK(install(finish,64));
    adaptive_ram_written((void *)(uintptr_t)1,ram,4);
    CHECK(adaptive_failed() && arm_adaptive_enabled);
    CHECK(arm_adaptive_try(&cpu,0,&result)==-1);
    adaptive_bind(&cpu);
    CHECK(!adaptive_failed()&&!adaptive_code_version(ram));
    CHECK(adaptive_retired(removed,1)==1 && removed[0]==finish);
    CHECK(adaptive_track_ram(ram,sizeof ram));
    CHECK(adaptive_code_version(ram)==1); /* new epoch, never old identity */
    adaptive_ram_written(cpu.bus->ctx,ram+sizeof ram-1,2);
    CHECK(adaptive_failed()&&!adaptive_code_version(ram));
    adaptive_bind(NULL);
    reset();
    CHECK(adaptive_track_ram(ram,sizeof ram));
    const arm_bus_t *saved_bus=cpu.bus;cpu.bus=NULL;
    adaptive_ram_written(NULL,ram,4);
    CHECK(adaptive_failed());
    cpu.bus=saved_bus;adaptive_bind(NULL);
    printf("PASS %u registry checks: ownership, stale epochs/mappings, exact binary cap, full retirement queue, active mutation fatal and recovery\n", checks);
    return 0;
}
