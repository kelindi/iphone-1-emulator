/*
 * S5LBox — S5L8900 machine tests.
 *
 * The headline test runs a hand-assembled bare-metal ARM payload on the
 * emulated SoC and reads back what it printed over the UART. This is the first
 * point where guest code produces observable output — the same channel iBoot
 * and XNU will use later.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include "snapshot.h"
#include "vfp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ------------------------------------------------------------------------ */

static void test_ram_readback(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    uint32_t v = 0xdeadbeef;
    s5l8900_load(&m, 0x100, &v, 4);
    CHECK(m.bus.read32(m.bus.ctx, 0x100) == 0xdeadbeefu,
          "ram readback = %08x", m.bus.read32(m.bus.ctx, 0x100));
    s5l8900_free(&m);
}

static void direct_write_test_interposer(void *ctx, uint32_t addr,
                                         uint32_t value) {
    (void)ctx; (void)addr; (void)value;
}

static void test_direct_ram_write_consent_is_fail_closed(void) {
    s5l8900_t m;
    CHECK(!s5l8900_set_direct_ram_writes(NULL, true),
          "NULL machine accepted direct writes");
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    CHECK(m.bus.host_ram_write == NULL,
          "generic machine unexpectedly opted into direct writes");

    CHECK(s5l8900_set_direct_ram_writes(&m, true),
          "canonical machine bus refused direct writes");
    CHECK(m.bus.host_ram_write == m.bus.host_ram,
          "write pointer source differs from proven RAM pointer source");

    m.cpu.dwrite[0].host = m.ram;
    m.cpu.dwrite[0].tag = 1u;
    m.cpu.dwrite[0].gen = m.cpu.tlb_gen;
    CHECK(s5l8900_set_direct_ram_writes(&m, false),
          "revoking direct writes failed");
    CHECK(m.bus.host_ram_write == NULL && m.cpu.dwrite[0].host == NULL,
          "revocation retained callback or derived pointer");

    /* Refusing an interposed bus must also revoke a stale earlier grant. */
    CHECK(s5l8900_set_direct_ram_writes(&m, true),
          "second canonical opt-in failed");
    m.cpu.dwrite[1].host = m.ram;
    void (*canonical_w32)(void *, uint32_t, uint32_t) = m.bus.write32;
    m.bus.write32 = direct_write_test_interposer;
    CHECK(!s5l8900_set_direct_ram_writes(&m, true),
          "interposed write bus was accepted");
    CHECK(m.bus.host_ram_write == NULL && m.cpu.dwrite[1].host == NULL,
          "refused opt-in did not fail closed");
    CHECK(s5l8900_set_direct_ram_writes(&m, false),
          "interposed bus could not explicitly revoke");

    m.bus.write32 = canonical_w32;
    CHECK(s5l8900_set_direct_ram_writes(&m, true),
          "restored canonical bus could not opt in");
    s5l8900_free(&m);
}

typedef struct {
    s5l8900_t *machine;
    uint32_t next_pc;
    uint32_t result;
    unsigned calls;
    bool handle;
} pre_step_fixture_t;

static bool pre_step_fixture_call(void *opaque) {
    pre_step_fixture_t *f = (pre_step_fixture_t *)opaque;
    if (!f || !f->machine) return false;
    f->calls++;
    if (!f->handle) return false;
    f->machine->cpu.r[0] = f->result;
    f->machine->cpu.r[15] = f->next_pc;
    return true;
}

static void test_pre_step_hook_is_bounded_and_fail_closed(void) {
    const uint32_t code[] = {
        UINT32_C(0xe3a00001), /* mov r0, #1 */
        UINT32_C(0xeafffffe), /* b .        */
    };
    const uint32_t target = 0u;
    const uint32_t duplicate[] = {0u, 0u};
    const uint32_t odd = 1u;
    s5l8900_t m;
    arm_status_t status = ARM_OK;
    pre_step_fixture_t fixture = {
        .machine = &m,
        .next_pc = 4u,
        .result = UINT32_C(0xdecafbad),
        .calls = 0u,
        .handle = true,
    };

    CHECK(!s5l8900_set_pre_step_hook(NULL, pre_step_fixture_call, &fixture,
                                     &target, 1u),
          "NULL machine accepted a pre-step hook");
    CHECK(s5l8900_init(&m, 0u, 1u << 20), "pre-step machine init failed");
    s5l8900_load(&m, 0u, code, sizeof code);
    m.cpu.r[15] = 0u;
    CHECK(!s5l8900_set_pre_step_hook(&m, pre_step_fixture_call, &fixture,
                                     NULL, 1u),
          "pre-step hook accepted a missing target table");
    CHECK(!s5l8900_set_pre_step_hook(&m, pre_step_fixture_call, &fixture,
                                     duplicate, 2u),
          "pre-step hook accepted duplicate targets");
    CHECK(!s5l8900_set_pre_step_hook(&m, pre_step_fixture_call, &fixture,
                                     &odd, 1u),
          "pre-step hook accepted an odd target");
    CHECK(s5l8900_set_pre_step_hook(&m, pre_step_fixture_call, &fixture,
                                    &target, 1u),
          "valid pre-step hook was refused");
    CHECK(s5l8900_run(&m, 1u, &status) == 1u && status == ARM_OK,
          "handled pre-step run did not retire its following instruction");
    CHECK(fixture.calls == 1u &&
              s5l8900_pre_step_matches(&m) == 1u &&
              s5l8900_pre_step_handled(&m) == 1u,
          "handled pre-step counts calls/matches/handled=%u/%llu/%llu",
          fixture.calls,
          (unsigned long long)s5l8900_pre_step_matches(&m),
          (unsigned long long)s5l8900_pre_step_handled(&m));
    CHECK(m.cpu.r[0] == fixture.result && m.cpu.r[15] == 4u &&
              m.cpu.cycles == 1u,
          "handled pre-step state r0/pc/cycles=%08x/%08x/%llu",
          m.cpu.r[0], m.cpu.r[15], (unsigned long long)m.cpu.cycles);

    fixture.handle = false;
    fixture.calls = 0u;
    m.cpu.r[0] = 0u;
    m.cpu.r[15] = 0u;
    CHECK(s5l8900_set_pre_step_hook(&m, pre_step_fixture_call, &fixture,
                                    &target, 1u),
          "refusal-path pre-step hook was refused");
    CHECK(s5l8900_run(&m, 1u, &status) == 1u && status == ARM_OK,
          "refused pre-step did not execute the guest instruction");
    CHECK(fixture.calls == 1u && m.cpu.r[0] == 1u && m.cpu.r[15] == 4u &&
              s5l8900_pre_step_matches(&m) == 1u &&
              s5l8900_pre_step_handled(&m) == 0u,
          "refused pre-step did not fail into the literal instruction");

    CHECK(s5l8900_set_pre_step_hook(&m, NULL, NULL, NULL, 0u),
          "clearing pre-step hook failed");
    CHECK(!s5l8900_pre_step_target(&m, target) &&
              s5l8900_pre_step_matches(&m) == 0u &&
              s5l8900_pre_step_handled(&m) == 0u,
          "cleared pre-step hook retained policy or counters");
    s5l8900_free(&m);
}

static void test_uart_status_is_ready(void) {
    /* A guest polling UTRSTAT must see the transmitter ready, or it spins. */
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 16);
    uint32_t st = m.bus.read32(m.bus.ctx, S5L8900_UART0_BASE + UART_UTRSTAT);
    CHECK((st & (1u << 2)) != 0, "UTRSTAT=%08x expect TX-empty set", st);
    s5l8900_free(&m);
}

static void test_bounds_check_cannot_overflow(void) {
    /* Regression: a 32-bit "(addr - base) + len <= size" wraps for addresses
     * near the top of the address space, letting a guest access index far
     * outside the RAM allocation. The guest controls every address, so this
     * must be rejected and merely counted as unmapped. */
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 20);
    (void)m.bus.read32(m.bus.ctx, 0xfffffffeu);
    CHECK(m.unmapped_reads == 1, "0xfffffffe read should be unmapped, not accepted");
    m.bus.write32(m.bus.ctx, 0xfffffffcu, 0xdeadbeefu);
    CHECK(m.unmapped_writes == 1, "0xfffffffc write should be unmapped, not accepted");
    /* The last legal word must still work. */
    m.bus.write32(m.bus.ctx, (1u << 20) - 4u, 0x12345678u);
    CHECK(m.bus.read32(m.bus.ctx, (1u << 20) - 4u) == 0x12345678u,
          "the final in-range word should still be accessible");
    CHECK(m.unmapped_writes == 1, "in-range write was wrongly rejected");
    s5l8900_free(&m);
}

static void test_unmapped_access_counted(void) {
    /* Accesses outside the memory map are counted, not silently swallowed. */
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 16);
    (void)m.bus.read32(m.bus.ctx, 0x70000000u);
    CHECK(m.unmapped_reads == 1, "unmapped_reads=%llu expect 1",
          (unsigned long long)m.unmapped_reads);
    s5l8900_free(&m);
}

/*
 * The bare-metal payload. Hand-assembled ARM that loads the UART base from a
 * literal and pushes "HI\n" out the transmit register, then spins.
 *
 *   00: LDR r0,[pc,#24]     ; r0 = UART0 base (literal at 0x20)
 *   04: MOV r1,#'H'
 *   08: STR r1,[r0,#0x20]   ; UTXH
 *   0c: MOV r1,#'I'
 *   10: STR r1,[r0,#0x20]
 *   14: MOV r1,#'\n'
 *   18: STR r1,[r0,#0x20]
 *   1c: B .                 ; park
 *   20: .word S5L8900_UART0_BASE
 */
static void test_bare_metal_uart_hello(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    const uint32_t payload[] = {
        0xe59f0018u,            /* LDR r0,[pc,#24]   */
        0xe3a01048u,            /* MOV r1,#0x48 'H'  */
        0xe5801020u,            /* STR r1,[r0,#0x20] */
        0xe3a01049u,            /* MOV r1,#0x49 'I'  */
        0xe5801020u,            /* STR r1,[r0,#0x20] */
        0xe3a0100au,            /* MOV r1,#0x0a '\n' */
        0xe5801020u,            /* STR r1,[r0,#0x20] */
        0xeafffffeu,            /* B .               */
        S5L8900_UART0_BASE      /* literal           */
    };
    s5l8900_load(&m, 0, payload, sizeof payload);

    arm_status_t st = ARM_OK;
    m.cpu.r[15] = 0;
    unsigned n = s5l8900_run(&m, 32, &st);

    CHECK(st == ARM_OK, "status=%d expect ARM_OK after %u steps", (int)st, n);
    m.uart0.tx[m.uart0.tx_len] = '\0';
    CHECK(strcmp(m.uart0.tx, "HI\n") == 0,
          "uart output = \"%s\" expect \"HI\\n\"", m.uart0.tx);
    CHECK(m.unmapped_writes == 0, "unexpected unmapped writes: %llu",
          (unsigned long long)m.unmapped_writes);

    printf("  [guest said] %s", m.uart0.tx);
    s5l8900_free(&m);
}

/*
 * The free-running counter is mach_absolute_time(). It must advance even when
 * no timer is armed: a counter that only runs while timer 4 is started reads
 * zero for the whole of early boot, and every delay loop in the kernel then
 * waits forever on a clock that never moves.
 */
static void test_timebase_runs_without_a_timer(void) {
    s5l_timer_t t; s5l_timer_reset(&t);
    s5l_timer_tick(&t, 1000);
    uint32_t lo = s5l_timer_read(&t, TIMER_TICKSLOW);
    CHECK(lo == 1000, "timebase=%u expect 1000 with no timer armed", lo);

    /* And it must carry into the high word rather than wrapping silently. */
    s5l_timer_reset(&t);
    for (unsigned i = 0; i < 5; i++) s5l_timer_tick(&t, 0xfffffffful / 4u);
    uint32_t hi = s5l_timer_read(&t, TIMER_TICKSHIGH);
    CHECK(hi == 1, "timebase high=%u expect 1 after passing 2^32", hi);
}

static void test_timer_period_is_exact(void) {
    /* One interrupt per armed interval, at a steady interval when the handler
     * re-arms the one-shot the way the kernel's set_decrementer does. */
    s5l_timer_t t; s5l_timer_reset(&t);
    s5l_timer_write(&t, TIMER4_COUNTBUF, 4);
    s5l_timer_write(&t, TIMER4_STATE, TIMER4_STATE_START | TIMER4_STATE_UPDATE);
    unsigned expiries = 0, last = 0; int deltas_ok = 1;
    for (unsigned tick = 1; tick <= 20; tick++) {
        if (s5l_timer_tick(&t, 1)) {
            if (last && (tick - last) != 4) deltas_ok = 0;
            last = tick; expiries++;
            s5l_timer_write(&t, TIMER_IRQACK, TIMER4_IRQ_BITS);   /* acknowledge */
            s5l_timer_write(&t, TIMER4_COUNTBUF, 4);
            s5l_timer_write(&t, TIMER4_STATE,
                            TIMER4_STATE_START | TIMER4_STATE_UPDATE);
        }
    }
    CHECK(expiries == 5, "expiries=%u expect 5 in 20 ticks with count 4", expiries);
    CHECK(deltas_ok, "expiry intervals were not a steady 4 ticks");
}

/*
 * The acknowledge mask is not a free choice: the kernel's FIQ handler writes
 * exactly TIMER4_IRQ_BITS. If the latch holds any bit that write does not
 * clear, the line stays asserted, the handler re-enters immediately, and the
 * boot presents as a hang rather than as a scheduler tick.
 */
static void test_timer_ack_mask_matches_the_kernels(void) {
    s5l_timer_t t; s5l_timer_reset(&t);
    s5l_timer_write(&t, TIMER4_COUNTBUF, 1);
    s5l_timer_write(&t, TIMER4_STATE, TIMER4_STATE_START | TIMER4_STATE_UPDATE);
    CHECK(s5l_timer_tick(&t, 1), "timer should latch an interrupt at expiry");
    s5l_timer_write(&t, TIMER_IRQACK, TIMER4_IRQ_BITS);
    CHECK(s5l_timer_read(&t, TIMER_IRQLATCH) == 0,
          "latch=%08x expect fully cleared by the kernel's ack mask",
          s5l_timer_read(&t, TIMER_IRQLATCH));
}

/* Slow, literal form of the old timer loop.  It is intentionally test-only:
 * compare the algebraic production implementation against it over awkward
 * phases without making a multi-billion-tick WFI wait run in host time. */
static void timer_tick_reference(s5l_timer_t *t, uint32_t ticks) {
    t->ticks += ticks;
    if (t->t4_state & TIMER4_STATE_START) {
        if (ticks && t->t4_value == 0) t->t4_value = t->t4_count;
        while (ticks-- && t->t4_value) {
            if (--t->t4_value == 0) {
                t->irqlatch |= TIMER4_IRQ_BITS;
                t->t4_state &= ~TIMER4_STATE_START;   /* one-shot */
            }
        }
    }
}

static void test_timer_lump_matches_literal_countdown(void) {
    /* Cover zero reloads, a live value larger than its reload, exact expiry
     * boundaries, multiple periods, stopped timers, and a pre-existing latch. */
    for (uint32_t period = 0; period <= 7; period++) {
        for (uint32_t value = 0; value <= 9; value++) {
            for (uint32_t ticks = 0; ticks <= 31; ticks++) {
                for (unsigned started = 0; started < 2; started++) {
                    s5l_timer_t fast, slow;
                    s5l_timer_reset(&fast);
                    fast.ticks = 0xfffffff0u;
                    fast.t4_count = period;
                    fast.t4_value = value;
                    fast.t4_state = started ? TIMER4_STATE_START : 0u;
                    fast.irqlatch = ((period + value + ticks) & 1u)
                                  ? TIMER4_IRQ_BITS : 0u;
                    slow = fast;
                    (void)s5l_timer_tick(&fast, ticks);
                    timer_tick_reference(&slow, ticks);
                    CHECK(fast.ticks == slow.ticks &&
                          fast.t4_value == slow.t4_value &&
                          fast.irqlatch == slow.irqlatch &&
                          fast.t4_state == slow.t4_state,
                          "p=%u v=%u ticks=%u start=%u: fast {%llu,%u,%x,%x} "
                          "slow {%llu,%u,%x,%x}",
                          period, value, ticks, started,
                          (unsigned long long)fast.ticks, fast.t4_value,
                          fast.irqlatch, fast.t4_state,
                          (unsigned long long)slow.ticks,
                          slow.t4_value, slow.irqlatch, slow.t4_state);
                }
            }
        }
    }

    /* This used to require UINT32_MAX host-loop iterations.  A one-shot
     * decrementer expires once and stops, whatever the overshoot. */
    s5l_timer_t huge;
    s5l_timer_reset(&huge);
    huge.t4_count = huge.t4_value = 3u;
    huge.t4_state = TIMER4_STATE_START;
    CHECK(s5l_timer_tick(&huge, UINT32_MAX),
          "a huge running interval must cross an expiry");
    CHECK(huge.ticks == UINT32_MAX && huge.t4_value == 0u &&
          huge.t4_state == 0u &&
          huge.irqlatch == TIMER4_IRQ_BITS,
          "huge tick left ticks=%llu value=%u latch=%08x",
          (unsigned long long)huge.ticks, huge.t4_value, huge.irqlatch);
}

static void test_wfi_fast_forwards_to_the_timer_boundary(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    const uint32_t program[] = {
        0xee070f90u,            /* MCR p15,0,r0,c7,c0,4  WFI */
        0xe3a0102au             /* MOV r1,#42             */
    };
    s5l8900_load(&m, 0, program, sizeof program);

    /* Three timebase ticks are eleven emulated CPU ticks away with this
     * fractional phase: ceil((3*4 - 1)/1) == 11. */
    m.cpu_hz = 4u;
    m.tb_hz = 1u;
    m.tb_accum = 1u;
    m.bus.write32(m.bus.ctx, S5L8900_TIMER_BASE + TIMER4_COUNTBUF, 3u);
    m.bus.write32(m.bus.ctx, S5L8900_TIMER_BASE + TIMER4_STATE,
                  TIMER4_STATE_START | TIMER4_STATE_UPDATE);
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE,
                  1u << S5L8900_IRQ_TIMER);
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTSELECT,
                  1u << S5L8900_IRQ_TIMER);             /* timer -> FIQ */

    /* F masks the FIQ exception, but ARM1176 WFI must still wake on the raw
     * asserted line.  No synthetic idle instructions may enter cpu.cycles. */
    m.cpu.r[15] = 0;
    m.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_F;
    arm_status_t st = ARM_OK;
    unsigned ran = s5l8900_run(&m, 1u, &st);
    CHECK(st == ARM_OK && ran == 1u &&
          m.cpu.r[15] == 4u && m.cpu.cycles == 1u,
          "WFI status=%d ran=%u pc=%08x retired=%llu",
          (int)st, ran, m.cpu.r[15], (unsigned long long)m.cpu.cycles);
    CHECK(m.timer.ticks == 3u && m.timer.t4_value == 0u,
          "timer stopped at ticks=%llu value=%u, expect exact expiry 3/0",
          (unsigned long long)m.timer.ticks, m.timer.t4_value);
    /* The runner accounts the WFI instruction's ordinary one active tick
     * after the synchronous wait.  At 4:1 that leaves fractional phase 1 and
     * does not cross a fourth timebase tick. */
    CHECK(m.tb_accum == 1u && m.cpu.fiq_line,
          "fraction=%llu fiq=%d expect 1/1 at the edge",
          (unsigned long long)m.tb_accum, (int)m.cpu.fiq_line);

    /* A masked wake resumes after the MCR instead of entering the handler. */
    st = arm_step(&m.cpu);
    CHECK(st == ARM_OK && m.cpu.r[1] == 42u && m.cpu.r[15] == 8u,
          "masked wake did not resume after WFI: status=%d r1=%u pc=%08x",
          (int)st, m.cpu.r[1], m.cpu.r[15]);
    s5l8900_free(&m);
}

typedef struct {
    unsigned calls;
    uint64_t last_ns;
    uint64_t total_ns;
    bool succeeds;
} wfi_pace_probe_t;

static bool wfi_pace_probe_sleep(void *ctx, uint64_t nanoseconds) {
    wfi_pace_probe_t *probe = ctx;
    if (!probe) return false;
    probe->calls++;
    probe->last_ns = nanoseconds;
    probe->total_ns += nanoseconds;
    return probe->succeeds;
}

typedef struct {
    uint64_t now_ns;
    unsigned now_calls;
    unsigned fail_on_call;
    unsigned sleep_calls;
    uint64_t last_sleep_ns;
    uint64_t sleep_overshoot_ns;
    bool succeeds;
} active_clock_probe_t;

static bool active_clock_probe_now(void *ctx, uint64_t *nanoseconds) {
    active_clock_probe_t *probe = ctx;
    if (!probe || !nanoseconds) return false;
    probe->now_calls++;
    if (!probe->succeeds ||
        (probe->fail_on_call && probe->now_calls == probe->fail_on_call))
        return false;
    *nanoseconds = probe->now_ns;
    return true;
}

static bool active_clock_probe_sleep(void *ctx, uint64_t nanoseconds) {
    active_clock_probe_t *probe = ctx;
    if (!probe) return false;
    probe->sleep_calls++;
    probe->last_sleep_ns = nanoseconds;
    probe->now_ns += nanoseconds;
    probe->now_ns += probe->sleep_overshoot_ns;
    return true;
}

static void fill_arm_nops(uint32_t *program, unsigned count) {
    for (unsigned i = 0u; i < count; i++) program[i] = 0xe1a00000u;
}

static void test_active_host_clock_is_optional_bounded_and_fail_closed(void) {
    uint32_t program[512];
    fill_arm_nops(program, sizeof program / sizeof program[0]);

    /* With no callback, active execution retains the literal historical
     * instruction clock exactly. */
    s5l8900_t deterministic;
    CHECK(s5l8900_init(&deterministic, 0, 1u << 20),
          "deterministic active-clock control init failed");
    s5l8900_load(&deterministic, 0, program, sizeof program);
    deterministic.cpu_hz = deterministic.tb_hz = 1000u;
    deterministic.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    arm_status_t st = ARM_OK;
    unsigned ran = s5l8900_run(&deterministic, 300u, &st);
    CHECK(st == ARM_OK && ran == 300u &&
          deterministic.timer.ticks == 300u &&
          deterministic.active_host_now == NULL,
          "default active timing changed: status=%d ran=%u ticks=%llu",
          (int)st, ran, (unsigned long long)deterministic.timer.ticks);
    s5l8900_free(&deterministic);

    s5l8900_t active;
    CHECK(s5l8900_init(&active, 0, 1u << 20),
          "active-clock machine init failed");
    s5l8900_load(&active, 0, program, sizeof program);
    active.cpu_hz = active.tb_hz = 1000u;
    active.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    active_clock_probe_t probe = {
        .now_ns = UINT64_C(1000000000), .succeeds = true
    };
    CHECK(!s5l8900_set_active_host_clock(NULL, active_clock_probe_now,
                                         &probe),
          "NULL machine accepted active clock");
    CHECK(s5l8900_set_active_host_clock(
              &active, active_clock_probe_now, &probe),
          "could not enable active host clock");

    /* Retiring 5000 instructions while the host sample is stationary advances
     * CPU work, but not the guest's physical clock. One sample occurs at the
     * 4096-retirement bound and another flushes the remainder at run exit. */
    ran = s5l8900_run(&active, 5000u, &st);
    CHECK(st == ARM_OK && ran == 5000u && active.timer.ticks == 0u &&
          active.cpu.cycles == 5000u && probe.now_calls == 3u,
          "stationary host clock advanced time or missed its bound: "
          "status=%d ran=%u ticks=%llu cycles=%llu calls=%u",
          (int)st, ran, (unsigned long long)active.timer.ticks,
          (unsigned long long)active.cpu.cycles, probe.now_calls);

    probe.now_ns += UINT64_C(5000000); /* five host milliseconds */
    ran = s5l8900_run(&active, 1u, &st);
    CHECK(st == ARM_OK && ran == 1u && active.timer.ticks == 5u &&
          active.active_clock_added_ticks == 5u,
          "5 ms host interval did not add five 1 kHz guest ticks: "
          "status=%d ran=%u ticks=%llu added=%llu",
          (int)st, ran, (unsigned long long)active.timer.ticks,
          (unsigned long long)active.active_clock_added_ticks);

    /* A long suspend-like interval contributes only the responsiveness cap;
     * the remaining 92 ms is deliberately discarded, not queued. */
    probe.now_ns += UINT64_C(100000000);
    ran = s5l8900_run(&active, 1u, &st);
    CHECK(st == ARM_OK && ran == 1u && active.timer.ticks == 13u &&
          active.active_clock_added_ticks == 13u &&
          active.active_clock_clamps == 1u,
          "long host interval was not capped: status=%d ran=%u ticks=%llu "
          "added=%llu clamps=%llu",
          (int)st, ran, (unsigned long long)active.timer.ticks,
          (unsigned long long)active.active_clock_added_ticks,
          (unsigned long long)active.active_clock_clamps);

    /* A backwards timestamp is untrustworthy. This run must immediately use
     * the old one-tick-per-retirement contract and re-anchor only next run. */
    probe.now_ns--;
    ran = s5l8900_run(&active, 3u, &st);
    CHECK(st == ARM_OK && ran == 3u && active.timer.ticks == 16u &&
          active.active_clock_failures == 1u &&
          !active.active_clock_anchor_valid,
          "backwards clock did not fail closed: status=%d ran=%u ticks=%llu "
          "failures=%llu anchor=%d",
          (int)st, ran, (unsigned long long)active.timer.ticks,
          (unsigned long long)active.active_clock_failures,
          (int)active.active_clock_anchor_valid);
    probe.now_ns += 2u;
    ran = s5l8900_run(&active, 2u, &st);
    CHECK(st == ARM_OK && ran == 2u && active.timer.ticks == 16u &&
          active.active_clock_anchor_valid,
          "clock did not re-anchor without manufacturing time: "
          "status=%d ran=%u ticks=%llu anchor=%d",
          (int)st, ran, (unsigned long long)active.timer.ticks,
          (int)active.active_clock_anchor_valid);

    CHECK(!s5l8900_set_active_host_clock(&active, NULL, &probe),
          "active-clock clear accepted stale context");
    CHECK(s5l8900_set_active_host_clock(&active, NULL, NULL) &&
          active.active_host_now == NULL &&
          active.active_host_now_ctx == NULL &&
          active.active_clock_updates == 0u &&
          active.active_clock_failures == 0u &&
          !active.active_clock_anchor_valid,
          "active-clock clear retained policy, evidence or anchor");
    s5l8900_free(&active);

    /* Failure after a successful anchor is more dangerous than failure at run
     * entry: all 300 instructions are pending. They must all receive their old
     * exact ticks, and the remainder of this run must stay on that old path. */
    s5l8900_t mid_batch_failure;
    CHECK(s5l8900_init(&mid_batch_failure, 0, 1u << 20),
          "mid-batch failure machine init failed");
    s5l8900_load(&mid_batch_failure, 0, program, sizeof program);
    mid_batch_failure.cpu_hz = mid_batch_failure.tb_hz = 1000u;
    mid_batch_failure.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    active_clock_probe_t failing = {
        .now_ns = UINT64_C(2000000000), .fail_on_call = 2u,
        .succeeds = true
    };
    CHECK(s5l8900_set_active_host_clock(
              &mid_batch_failure, active_clock_probe_now, &failing),
          "could not enable mid-batch failure oracle");
    ran = s5l8900_run(&mid_batch_failure, 300u, &st);
    CHECK(st == ARM_OK && ran == 300u &&
          mid_batch_failure.timer.ticks == 300u &&
          mid_batch_failure.active_clock_failures == 1u &&
          failing.now_calls == 2u,
          "mid-batch failure lost retirement time: status=%d ran=%u "
          "ticks=%llu failures=%llu calls=%u",
          (int)st, ran,
          (unsigned long long)mid_batch_failure.timer.ticks,
          (unsigned long long)mid_batch_failure.active_clock_failures,
          failing.now_calls);
    s5l8900_free(&mid_batch_failure);
}

static void test_active_host_clock_does_not_double_count_paced_wfi(void) {
    const uint32_t wfi = 0xee070f90u;
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20),
          "active WFI machine init failed");
    s5l8900_load(&m, 0, &wfi, sizeof wfi);
    m.cpu_hz = m.tb_hz = 1000u;
    m.timer.t4_count = m.timer.t4_value = 4u;
    m.timer.t4_state = TIMER4_STATE_START;
    m.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    m.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;

    active_clock_probe_t probe = { .succeeds = true };
    CHECK(s5l8900_set_active_host_clock(
              &m, active_clock_probe_now, &probe) &&
          s5l8900_set_wfi_host_pacing(
              &m, active_clock_probe_sleep, &probe),
          "could not enable combined active/WFI clock policy");
    arm_status_t st = ARM_OK;
    unsigned ran = s5l8900_run(&m, 1u, &st);
    CHECK(st == ARM_OK && ran == 1u &&
          probe.sleep_calls == 1u && probe.last_sleep_ns == UINT64_C(4000000) &&
          m.timer.ticks == 4u && m.active_clock_added_ticks == 0u &&
          m.active_clock_updates == 1u &&
          m.active_clock_guest_ticks_since_sync == 0u,
          "paced WFI was counted twice: status=%d ran=%u sleeps=%u ns=%llu "
          "ticks=%llu added=%llu updates=%llu pending=%llu",
          (int)st, ran, probe.sleep_calls,
          (unsigned long long)probe.last_sleep_ns,
          (unsigned long long)m.timer.ticks,
          (unsigned long long)m.active_clock_added_ticks,
          (unsigned long long)m.active_clock_updates,
          (unsigned long long)m.active_clock_guest_ticks_since_sync);
    s5l8900_free(&m);
}

static void test_active_host_clock_preserves_only_bounded_wfi_oversleep(void) {
    const uint32_t wfi = 0xee070f90u;
    arm_status_t st = ARM_OK;

    /* The guest deliberately advances one full 8 ms WFI slice. If the host
     * actually sleeps for 10 ms, the unmodeled 2 ms is real elapsed time, not
     * a suspend. It must survive without tripping the 8 ms residual cap. */
    s5l8900_t ordinary;
    CHECK(s5l8900_init(&ordinary, 0, 1u << 20),
          "ordinary WFI oversleep machine init failed");
    s5l8900_load(&ordinary, 0, &wfi, sizeof wfi);
    ordinary.cpu_hz = ordinary.tb_hz = 1000u;
    ordinary.timer.t4_count = ordinary.timer.t4_value = 20u;
    ordinary.timer.t4_state = TIMER4_STATE_START;
    ordinary.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    ordinary.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    active_clock_probe_t ordinary_probe = {
        .sleep_overshoot_ns = UINT64_C(2000000), .succeeds = true
    };
    CHECK(s5l8900_set_active_host_clock(
              &ordinary, active_clock_probe_now, &ordinary_probe) &&
          s5l8900_set_wfi_host_pacing(
              &ordinary, active_clock_probe_sleep, &ordinary_probe),
          "could not enable ordinary WFI oversleep oracle");
    unsigned ran = s5l8900_run(&ordinary, 1u, &st);
    CHECK(st == ARM_OK && ran == 1u &&
          ordinary_probe.sleep_calls == 1u &&
          ordinary_probe.last_sleep_ns == UINT64_C(8000000) &&
          ordinary.timer.ticks == 10u &&
          ordinary.active_clock_added_ticks == 2u &&
          ordinary.active_clock_clamps == 0u &&
          ordinary.active_clock_guest_ticks_since_sync == 0u,
          "ordinary WFI oversleep was lost or clamped: status=%d ran=%u "
          "sleeps=%u ns=%llu ticks=%llu added=%llu clamps=%llu pending=%llu",
          (int)st, ran, ordinary_probe.sleep_calls,
          (unsigned long long)ordinary_probe.last_sleep_ns,
          (unsigned long long)ordinary.timer.ticks,
          (unsigned long long)ordinary.active_clock_added_ticks,
          (unsigned long long)ordinary.active_clock_clamps,
          (unsigned long long)ordinary.active_clock_guest_ticks_since_sync);
    s5l8900_free(&ordinary);

    /* A 100 ms host sleep after the same modeled 8 ms wait leaves 92 ms
     * unmodeled. That is suspend-like: add one bounded 8 ms residual and
     * discard the rest rather than delivering a 100 ms guest-time burst. */
    s5l8900_t suspended;
    CHECK(s5l8900_init(&suspended, 0, 1u << 20),
          "suspend-like WFI oversleep machine init failed");
    s5l8900_load(&suspended, 0, &wfi, sizeof wfi);
    suspended.cpu_hz = suspended.tb_hz = 1000u;
    suspended.timer.t4_count = suspended.timer.t4_value = 20u;
    suspended.timer.t4_state = TIMER4_STATE_START;
    suspended.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    suspended.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    active_clock_probe_t suspended_probe = {
        .sleep_overshoot_ns = UINT64_C(92000000), .succeeds = true
    };
    CHECK(s5l8900_set_active_host_clock(
              &suspended, active_clock_probe_now, &suspended_probe) &&
          s5l8900_set_wfi_host_pacing(
              &suspended, active_clock_probe_sleep, &suspended_probe),
          "could not enable suspend-like WFI oversleep oracle");
    ran = s5l8900_run(&suspended, 1u, &st);
    CHECK(st == ARM_OK && ran == 1u &&
          suspended_probe.sleep_calls == 1u &&
          suspended.timer.ticks == 16u &&
          suspended.active_clock_added_ticks == 8u &&
          suspended.active_clock_clamps == 1u &&
          suspended.active_clock_guest_ticks_since_sync == 0u,
          "suspend-like WFI residual escaped its cap: status=%d ran=%u "
          "sleeps=%u ticks=%llu added=%llu clamps=%llu pending=%llu",
          (int)st, ran, suspended_probe.sleep_calls,
          (unsigned long long)suspended.timer.ticks,
          (unsigned long long)suspended.active_clock_added_ticks,
          (unsigned long long)suspended.active_clock_clamps,
          (unsigned long long)suspended.active_clock_guest_ticks_since_sync);
    s5l8900_free(&suspended);
}

static void test_active_host_clock_refreshes_devices_without_oversampling(void) {
    const uint32_t program[] = {
        0xe5801020u, /* STR r1,[r0,#0x20] -- UART UTXH */
        0xeafffffdu  /* B 0 */
    };
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20),
          "active device-boundary machine init failed");
    s5l8900_load(&m, 0, program, sizeof program);
    m.cpu.r[0] = S5L8900_UART0_BASE;
    m.cpu.r[1] = 'A';
    m.cpu_hz = m.tb_hz = 1000u;
    m.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;

    active_clock_probe_t probe = { .succeeds = true };
    CHECK(s5l8900_set_active_host_clock(
              &m, active_clock_probe_now, &probe),
          "could not enable device-boundary clock oracle");
    arm_status_t st = ARM_OK;
    unsigned ran = s5l8900_run(&m, 100u, &st);
    /* Fifty stores each dirty the machine and still require an immediate zero
     * tick before the following instruction. They do not require fifty calls
     * to a stationary host clock: entry plus the run-exit flush are enough. */
    CHECK(st == ARM_OK && ran == 100u && m.uart0.tx_len == 50u &&
          m.timer.ticks == 0u && probe.now_calls == 2u &&
          m.active_clock_updates == 1u,
          "device refresh sampled host clock per MMIO: status=%d ran=%u "
          "tx=%zu ticks=%llu calls=%u updates=%llu",
          (int)st, ran, m.uart0.tx_len,
          (unsigned long long)m.timer.ticks, probe.now_calls,
          (unsigned long long)m.active_clock_updates);
    s5l8900_free(&m);
}

static void test_wfi_host_pacing_is_optional_exact_and_yields(void) {
    const uint32_t program[] = {
        0xee070f90u,            /* WFI */
        0xe3a0102au             /* MOV r1,#42 */
    };

    s5l8900_t paced;
    CHECK(s5l8900_init(&paced, 0, 1u << 20), "paced machine init failed");
    s5l8900_load(&paced, 0, program, sizeof program);
    /* Three timebase ticks from fractional phase 1 at 4:1 need eleven CPU
     * ticks. At 4 kHz that is exactly 2,750,000 ns, below the host slice. */
    paced.cpu_hz = 4000u;
    paced.tb_hz = 1000u;
    paced.tb_accum = 1000u;
    paced.timer.t4_count = paced.timer.t4_value = 3u;
    paced.timer.t4_state = TIMER4_STATE_START;
    paced.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    paced.vic[0].select = 1u << S5L8900_IRQ_TIMER;
    paced.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_F;

    wfi_pace_probe_t probe = { .succeeds = true };
    CHECK(s5l8900_set_wfi_host_pacing(&paced, wfi_pace_probe_sleep, &probe),
          "could not enable WFI host pacing");
    arm_status_t st = ARM_OK;
    unsigned ran = s5l8900_run(&paced, 2u, &st);
    CHECK(st == ARM_OK && ran == 1u && paced.cpu.r[15] == 4u &&
          paced.cpu.r[1] == 0u,
          "paced run did not yield after WFI: status=%d ran=%u pc=%08x r1=%u",
          (int)st, ran, paced.cpu.r[15], paced.cpu.r[1]);
    CHECK(probe.calls == 1u && probe.last_ns == UINT64_C(2750000) &&
          probe.total_ns == UINT64_C(2750000),
          "paced wait calls=%u last=%llu total=%llu, expected 1/2750000",
          probe.calls, (unsigned long long)probe.last_ns,
          (unsigned long long)probe.total_ns);
    CHECK(paced.timer.ticks == 3u && paced.tb_accum == 1000u &&
          paced.cpu.fiq_line,
          "paced WFI missed its exact edge: ticks=%llu frac=%llu fiq=%d",
          (unsigned long long)paced.timer.ticks,
          (unsigned long long)paced.tb_accum, (int)paced.cpu.fiq_line);
    CHECK(paced.wfi_paced_waits == 1u &&
          paced.wfi_paced_wait_ns == UINT64_C(2750000) &&
          paced.wfi_paced_partial_advances == 0u &&
          paced.wfi_paced_failures == 0u,
          "paced evidence waits=%llu ns=%llu partial=%llu failures=%llu",
          (unsigned long long)paced.wfi_paced_waits,
          (unsigned long long)paced.wfi_paced_wait_ns,
          (unsigned long long)paced.wfi_paced_partial_advances,
          (unsigned long long)paced.wfi_paced_failures);

    /* The yield is one-shot. The next bounded run executes ordinary code and
     * does not inherit a stale stop request from the preceding WFI. */
    ran = s5l8900_run(&paced, 1u, &st);
    CHECK(st == ARM_OK && ran == 1u && paced.cpu.r[1] == 42u,
          "paced yield leaked into the next run: status=%d ran=%u r1=%u",
          (int)st, ran, paced.cpu.r[1]);
    s5l8900_free(&paced);

    /* Default machines have no host wait and retain the old deterministic
     * behavior: the same two-instruction slice crosses WFI and the MOV. */
    s5l8900_t deterministic;
    CHECK(s5l8900_init(&deterministic, 0, 1u << 20),
          "deterministic machine init failed");
    s5l8900_load(&deterministic, 0, program, sizeof program);
    deterministic.cpu_hz = 4000u;
    deterministic.tb_hz = 1000u;
    deterministic.tb_accum = 1000u;
    deterministic.timer.t4_count = deterministic.timer.t4_value = 3u;
    deterministic.timer.t4_state = TIMER4_STATE_START;
    deterministic.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    deterministic.vic[0].select = 1u << S5L8900_IRQ_TIMER;
    deterministic.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_F;
    ran = s5l8900_run(&deterministic, 2u, &st);
    CHECK(st == ARM_OK && ran == 2u && deterministic.cpu.r[1] == 42u &&
          deterministic.wfi_paced_waits == 0u,
          "default WFI changed: status=%d ran=%u r1=%u waits=%llu",
          (int)st, ran, deterministic.cpu.r[1],
          (unsigned long long)deterministic.wfi_paced_waits);
    s5l8900_free(&deterministic);
}

static void test_wfi_host_pacing_bounds_long_and_failed_waits(void) {
    const uint32_t wfi = 0xee070f90u;

    s5l8900_t partial;
    CHECK(s5l8900_init(&partial, 0, 1u << 20),
          "partial pacing machine init failed");
    s5l8900_load(&partial, 0, &wfi, sizeof wfi);
    partial.cpu_hz = 1000u;
    partial.tb_hz = 1u;
    partial.timer.t4_count = partial.timer.t4_value = 1u;
    partial.timer.t4_state = TIMER4_STATE_START;
    partial.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    partial.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I;
    wfi_pace_probe_t partial_probe = { .succeeds = true };
    CHECK(s5l8900_set_wfi_host_pacing(
              &partial, wfi_pace_probe_sleep, &partial_probe),
          "could not enable partial WFI pacing");

    arm_status_t st = ARM_OK;
    unsigned ran = s5l8900_run(&partial, 2u, &st);
    /* Eight paced CPU ticks fit in the 8 ms host slice; the retired WFI adds
     * one ordinary active tick afterward. Neither reaches the 1 s timer edge. */
    CHECK(st == ARM_OK && ran == 1u &&
          partial_probe.last_ns == S5L8900_WFI_PACE_SLICE_NS &&
          partial.timer.ticks == 0u && partial.tb_accum == 9u &&
          partial.timer.t4_value == 1u && !partial.cpu.irq_line,
          "long WFI was not sliced safely: status=%d ran=%u wait=%llu "
          "ticks=%llu frac=%llu value=%u irq=%d",
          (int)st, ran, (unsigned long long)partial_probe.last_ns,
          (unsigned long long)partial.timer.ticks,
          (unsigned long long)partial.tb_accum,
          partial.timer.t4_value, (int)partial.cpu.irq_line);
    CHECK(partial.wfi_paced_partial_advances == 1u &&
          partial.wfi_paced_failures == 0u,
          "long WFI evidence partial=%llu failures=%llu",
          (unsigned long long)partial.wfi_paced_partial_advances,
          (unsigned long long)partial.wfi_paced_failures);
    s5l8900_free(&partial);

    s5l8900_t failed;
    CHECK(s5l8900_init(&failed, 0, 1u << 20),
          "failed pacing machine init failed");
    s5l8900_load(&failed, 0, &wfi, sizeof wfi);
    failed.cpu_hz = 4000u;
    failed.tb_hz = 1000u;
    failed.tb_accum = 1000u;
    failed.timer.t4_count = failed.timer.t4_value = 3u;
    failed.timer.t4_state = TIMER4_STATE_START;
    failed.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    failed.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I;
    wfi_pace_probe_t failed_probe = { .succeeds = false };
    CHECK(s5l8900_set_wfi_host_pacing(
              &failed, wfi_pace_probe_sleep, &failed_probe),
          "could not enable failing WFI pacing oracle");
    ran = s5l8900_run(&failed, 2u, &st);
    CHECK(st == ARM_OK && ran == 1u && failed.timer.ticks == 0u &&
          failed.tb_accum == 2000u && failed.timer.t4_value == 3u &&
          failed.wfi_paced_failures == 1u,
          "failed host wait manufactured time: status=%d ran=%u ticks=%llu "
          "frac=%llu value=%u failures=%llu",
          (int)st, ran, (unsigned long long)failed.timer.ticks,
          (unsigned long long)failed.tb_accum, failed.timer.t4_value,
          (unsigned long long)failed.wfi_paced_failures);
    CHECK(!s5l8900_set_wfi_host_pacing(&failed, NULL, &failed_probe),
          "clear accepted a stale pacing context");
    CHECK(s5l8900_set_wfi_host_pacing(&failed, NULL, NULL) &&
          failed.wfi_host_sleep == NULL && failed.wfi_paced_waits == 0u,
          "clean pacing disable did not reset host policy/evidence");
    s5l8900_free(&failed);
}

static void test_wfi_unmasked_fiq_uses_the_post_mcr_return_link(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t wfi = 0xee070f90u;
    s5l8900_load(&m, 0, &wfi, sizeof wfi);
    m.cpu_hz = m.tb_hz = 1u;
    m.timer.t4_count = m.timer.t4_value = 2u;
    m.timer.t4_state = TIMER4_STATE_START;
    m.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    m.vic[0].select = 1u << S5L8900_IRQ_TIMER;
    m.cpu.cpsr = ARM_MODE_SYS;                  /* IRQ and FIQ both enabled */

    arm_status_t st = arm_step(&m.cpu);         /* WFI reaches the edge */
    CHECK(st == ARM_OK && m.cpu.r[15] == 4u && m.cpu.fiq_line,
          "WFI did not complete at pending FIQ: status=%d pc=%08x fiq=%d",
          (int)st, m.cpu.r[15], (int)m.cpu.fiq_line);
    st = arm_step(&m.cpu);                      /* interrupt is sampled here */
    CHECK(st == ARM_OK && m.cpu.r[15] == ARM_VEC_FIQ &&
          (m.cpu.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_FIQ,
          "FIQ entry status=%d pc=%08x mode=%02x",
          (int)st, m.cpu.r[15], m.cpu.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(m.cpu.r[14] == 8u && m.cpu.cycles == 2u,
          "lr_fiq=%08x cycles=%llu expect WFI+8 and two retired operations",
          m.cpu.r[14], (unsigned long long)m.cpu.cycles);
    s5l8900_free(&m);
}

static void test_wfi_pending_line_completes_without_advancing_time(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t wfi = 0xee070f90u;
    s5l8900_load(&m, 0, &wfi, sizeof wfi);

    /* Make a software IRQ both pending and visible before WFI.  I masks
     * exception entry at arm_step's first sample, but not WFI wakeup. */
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE, 1u << 3);
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_SOFTINT, 1u << 3);
    s5l8900_tick(&m, 0);
    CHECK(m.cpu.irq_line, "software IRQ should already be asserted");
    m.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I;
    arm_status_t st = arm_step(&m.cpu);
    CHECK(st == ARM_OK && m.cpu.r[15] == 4u && m.cpu.cycles == 1u,
          "pending WFI status=%d pc=%08x cycles=%llu",
          (int)st, m.cpu.r[15], (unsigned long long)m.cpu.cycles);
    CHECK(m.timer.ticks == 0u && m.tb_accum == 0u,
          "already-pending wake advanced time to %llu/%llu",
          (unsigned long long)m.timer.ticks,
          (unsigned long long)m.tb_accum);
    s5l8900_free(&m);
}

static void test_wfi_stops_at_earliest_deliverable_device_edge(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t wfi = 0xee070f90u;
    s5l8900_load(&m, 0, &wfi, sizeof wfi);
    m.cpu_hz = m.tb_hz = 1u;

    /* Timer would fire in ten ticks.  A scanout frame is three ticks away and
     * is enabled through the VIC, so WFI must stop there and leave seven timer
     * ticks outstanding rather than coalescing across the CLCD interrupt. */
    m.timer.t4_count = m.timer.t4_value = 10u;
    m.timer.t4_state = TIMER4_STATE_START;
    m.clcd.scanning = true;
    m.clcd.ctrl = CLCD_CTRL_ENABLE;
    m.clcd.gate = 1u;
    m.clcd.frame_ticks = 4u;
    m.clcd.frame_accum = 1u;
    m.clcd.intmask = CLCD_INT_FRAME;
    m.vic[0].enable = (1u << S5L8900_IRQ_TIMER) | (1u << S5L8900_IRQ_CLCD);
    m.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;

    arm_status_t st = arm_step(&m.cpu);
    CHECK(st == ARM_OK && m.timer.ticks == 3u,
          "earliest-edge WFI status=%d ticks=%llu expect 3",
          (int)st, (unsigned long long)m.timer.ticks);
    CHECK(m.timer.t4_value == 7u && m.timer.irqlatch == 0u,
          "timer crossed later edge: value=%u latch=%08x",
          m.timer.t4_value, m.timer.irqlatch);
    CHECK(m.clcd.frames == 1u && m.clcd.frame_accum == 0u && m.cpu.irq_line,
          "CLCD edge frames=%llu phase=%u irq=%d",
          (unsigned long long)m.clcd.frames, m.clcd.frame_accum,
          (int)m.cpu.irq_line);
    CHECK(m.cpu.cycles == 1u, "idle elapsed time polluted retired count: %llu",
          (unsigned long long)m.cpu.cycles);
    s5l8900_free(&m);
}

static void test_wfi_lump_preserves_non_waking_device_side_effects(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t wfi = 0xee070f90u;
    s5l8900_load(&m, 0, &wfi, sizeof wfi);
    m.cpu_hz = m.tb_hz = 1u;

    m.timer.t4_count = m.timer.t4_value = 10u;
    m.timer.t4_state = TIMER4_STATE_START;
    m.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    m.clcd.scanning = true;
    m.clcd.ctrl = CLCD_CTRL_ENABLE;
    m.clcd.gate = 1u;
    m.clcd.frame_ticks = 3u;
    m.clcd.frame_accum = 0u;
    m.clcd.intmask = 0u;                       /* frames cannot wake the CPU */
    m.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I;

    arm_status_t st = arm_step(&m.cpu);
    CHECK(st == ARM_OK && m.timer.ticks == 10u && m.cpu.irq_line,
          "timer wake status=%d ticks=%llu irq=%d",
          (int)st, (unsigned long long)m.timer.ticks, (int)m.cpu.irq_line);
    /* Three VBL boundaries occurred while the core slept.  Their level latch
     * coalesces, but the visibility counter and residual phase must not be
     * skipped merely because CLCD was not the selected wake source. */
    CHECK(m.clcd.frames == 3u && m.clcd.frame_accum == 1u &&
          (m.clcd.intstatus & CLCD_INT_FRAME) != 0u,
          "sleep lost CLCD state: frames=%llu phase=%u status=%08x",
          (unsigned long long)m.clcd.frames, m.clcd.frame_accum,
          m.clcd.intstatus);
    s5l8900_free(&m);
}

static void test_wfi_no_event_falls_back_without_time_or_host_hang(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t program[] = { 0xee070f90u, 0xe3a02007u };
    s5l8900_load(&m, 0, program, sizeof program);

    /* An armed timer behind a disabled VIC cannot wake an ARM1176 in standby.
     * Do not run it to an arbitrary boundary and do not block the host forever. */
    m.timer.t4_count = m.timer.t4_value = 9u;
    m.timer.t4_state = TIMER4_STATE_START;
    m.cpu.cpsr = ARM_MODE_SYS;
    arm_status_t st = arm_step(&m.cpu);
    CHECK(st == ARM_OK && m.cpu.r[15] == 4u && m.cpu.cycles == 1u,
          "no-event WFI status=%d pc=%08x cycles=%llu",
          (int)st, m.cpu.r[15], (unsigned long long)m.cpu.cycles);
    CHECK(m.timer.ticks == 0u && m.timer.t4_value == 9u &&
          !m.cpu.irq_line && !m.cpu.fiq_line,
          "no-event fallback changed devices: ticks=%llu value=%u irq=%d fiq=%d",
          (unsigned long long)m.timer.ticks, m.timer.t4_value,
          (int)m.cpu.irq_line, (int)m.cpu.fiq_line);
    st = arm_step(&m.cpu);
    CHECK(st == ARM_OK && m.cpu.r[2] == 7u,
          "host did not make progress after fallback: status=%d r2=%u",
          (int)st, m.cpu.r[2]);
    s5l8900_free(&m);

    /* Enabled routes are still not future events when their autonomous
     * counters are zero/disabled.  This separate shape catches accidental
     * division by zero and UINT32 wrap in the event-distance calculation. */
    s5l8900_t zero;
    CHECK(s5l8900_init(&zero, 0, 1u << 20), "zero-event machine init failed");
    s5l8900_load(&zero, 0, &program[0], sizeof program[0]);
    zero.timer.t4_state = TIMER4_STATE_START;
    zero.timer.t4_count = zero.timer.t4_value = 0u;
    zero.clcd.scanning = true;
    zero.clcd.ctrl = CLCD_CTRL_ENABLE;
    zero.clcd.gate = 1u;
    zero.clcd.frame_ticks = 0u;
    zero.clcd.intmask = CLCD_INT_FRAME;
    zero.vic[0].enable = (1u << S5L8900_IRQ_TIMER) |
                         (1u << S5L8900_IRQ_CLCD);
    zero.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    st = arm_step(&zero.cpu);
    CHECK(st == ARM_OK && zero.cpu.r[15] == 4u &&
          zero.timer.ticks == 0u && zero.tb_accum == 0u,
          "zero-event fallback status=%d pc=%08x ticks=%llu fraction=%llu",
          (int)st, zero.cpu.r[15], (unsigned long long)zero.timer.ticks,
          (unsigned long long)zero.tb_accum);
    s5l8900_free(&zero);
}

/*
 * CHARACTERISATION: exactly how far WFI fast-forwards, in every shape the three
 * modelled autonomous sources can be in.
 *
 * Every number below was measured against the hardcoded if-chain that decided
 * this before the wake-source table existed. Turning that chain into data is a
 * refactor, so all of them have to survive it unchanged — which is the whole
 * reason this test exists rather than only the new-source tests further down.
 *
 * The clocks are 1:1 here so one emulated CPU tick is one timebase tick, and
 * the distance is read off the free-running counter, which advances
 * unconditionally. arm_step() is used rather than s5l8900_run() so the WFI's
 * own retired tick does not land on top of the idle interval being measured.
 */
struct wfi_edge_case {
    const char *name;
    uint32_t vic_enable;
    uint32_t t4_state, t4_count, t4_value;
    bool     clcd_running;
    uint32_t clcd_intmask, clcd_frame_ticks, clcd_frame_accum;
    bool     tvout_running, tvout_unmasked;
    uint32_t tvout_frame_ticks, tvout_frame_accum;
    uint32_t expect_ticks;      /* timebase ticks the idle wait may cover */
    bool     expect_wake;       /* a CPU line asserted by the time it ends */
};

static void wfi_edge_case_run(const struct wfi_edge_case *c,
                              uint32_t *ticks, bool *wake) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 16), "%s: machine init failed", c->name);
    const uint32_t wfi = 0xee070f90u;
    s5l8900_load(&m, 0, &wfi, sizeof wfi);
    m.cpu_hz = m.tb_hz = 1u;

    m.vic[0].enable  = c->vic_enable;
    m.timer.t4_state = c->t4_state;
    m.timer.t4_count = c->t4_count;
    m.timer.t4_value = c->t4_value;

    m.clcd.scanning    = c->clcd_running;
    m.clcd.ctrl        = c->clcd_running ? CLCD_CTRL_ENABLE : 0u;
    m.clcd.gate        = c->clcd_running ? 1u : 0u;
    m.clcd.intmask     = c->clcd_intmask;
    m.clcd.frame_ticks = c->clcd_frame_ticks;
    m.clcd.frame_accum = c->clcd_frame_accum;

    /* Poke the TV-out run bits directly: writing them through the bus would
     * reset the frame phase on the running transition, which is the state
     * being set up here. */
    if (c->tvout_running) {
        m.tvout.regs[S5L_TVOUT_BANK_MIXER][0] = TVOUT_RUN;
        m.tvout.regs[S5L_TVOUT_BANK_SDO][0]   = TVOUT_RUN;
    }
    m.tvout.regs[S5L_TVOUT_BANK_SDO][TVOUT_SDO_IRQMASK / 4u] =
        c->tvout_unmasked ? 0u : TVOUT_SDO_MASK_VSYNC;
    m.tvout.frame_ticks = c->tvout_frame_ticks;
    m.tvout.frame_accum = c->tvout_frame_accum;

    /* Both exceptions masked: an ARM1176 still wakes on the raw line, and the
     * step must not vector into a handler and confuse the measurement. */
    m.cpu.r[15] = 0;
    m.cpu.cpsr  = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    arm_status_t st = arm_step(&m.cpu);
    CHECK(st == ARM_OK && m.cpu.r[15] == 4u && m.cpu.cycles == 1u,
          "%s: WFI status=%d pc=%08x cycles=%llu",
          c->name, (int)st, m.cpu.r[15], (unsigned long long)m.cpu.cycles);

    *ticks = (uint32_t)m.timer.ticks;
    *wake  = m.cpu.irq_line || m.cpu.fiq_line;
    s5l8900_free(&m);
}

static void test_wfi_existing_source_edges_are_unchanged(void) {
    const uint32_t T = 1u << S5L8900_IRQ_TIMER;
    const uint32_t C = 1u << S5L8900_IRQ_CLCD;
    const uint32_t V = 1u << S5L8900_IRQ_TVOUT;
    const struct wfi_edge_case cases[] = {
      /* --- timer 4's decrementer ------------------------------------------ */
      { .name = "timer live value", .vic_enable = T,
        .t4_state = TIMER4_STATE_START, .t4_count = 10u, .t4_value = 10u,
        .expect_ticks = 10u, .expect_wake = true },
      { .name = "timer reloads from a zero live value", .vic_enable = T,
        .t4_state = TIMER4_STATE_START, .t4_count = 6u, .t4_value = 0u,
        .expect_ticks = 6u, .expect_wake = true },
      { .name = "timer live value below its reload", .vic_enable = T,
        .t4_state = TIMER4_STATE_START, .t4_count = 10u, .t4_value = 3u,
        .expect_ticks = 3u, .expect_wake = true },
      { .name = "timer stopped at zero cannot expire", .vic_enable = T,
        .t4_state = TIMER4_STATE_START, .t4_count = 0u, .t4_value = 0u,
        .expect_ticks = 0u },
      { .name = "timer armed but not started", .vic_enable = T,
        .t4_count = 9u, .t4_value = 9u, .expect_ticks = 0u },
      { .name = "timer running behind a masked VIC line",
        .t4_state = TIMER4_STATE_START, .t4_count = 9u, .t4_value = 9u,
        .expect_ticks = 0u },

      /* --- CLCD frame (VBL) ----------------------------------------------- */
      { .name = "clcd part way through a frame", .vic_enable = C,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 1u,
        .expect_ticks = 3u, .expect_wake = true },
      { .name = "clcd at a frame boundary", .vic_enable = C,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 0u,
        .expect_ticks = 4u, .expect_wake = true },
      /* A phase past its own period is malformed. The zero-tick refresh at the
       * head of the wait normalises it and latches the frame there, so the wait
       * ends immediately rather than reaching the distance calculation. */
      { .name = "clcd phase past its period", .vic_enable = C,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 9u,
        .expect_ticks = 0u, .expect_wake = true },
      { .name = "clcd with the VBL disabled by a zero period", .vic_enable = C,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 0u, .expect_ticks = 0u },
      { .name = "clcd frame masked off at the controller", .vic_enable = C,
        .clcd_running = true, .clcd_intmask = 0u,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 1u, .expect_ticks = 0u },
      { .name = "clcd not scanning", .vic_enable = C,
        .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 1u, .expect_ticks = 0u },
      { .name = "clcd behind a masked VIC line",
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 1u, .expect_ticks = 0u },

      /* --- TV-out SDO VSYNC ------------------------------------------------ */
      { .name = "tvout part way through a frame", .vic_enable = V,
        .tvout_running = true, .tvout_unmasked = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 5u,
        .expect_ticks = 2u, .expect_wake = true },
      { .name = "tvout vsync masked at the SDO", .vic_enable = V,
        .tvout_running = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 5u, .expect_ticks = 0u },
      { .name = "tvout timing engines stopped", .vic_enable = V,
        .tvout_unmasked = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 5u, .expect_ticks = 0u },
      { .name = "tvout behind a masked VIC line",
        .tvout_running = true, .tvout_unmasked = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 5u, .expect_ticks = 0u },

      /* --- the minimum across several live sources ------------------------- */
      { .name = "timer 10, clcd 3: clcd is nearer", .vic_enable = T | C,
        .t4_state = TIMER4_STATE_START, .t4_count = 10u, .t4_value = 10u,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 1u,
        .expect_ticks = 3u, .expect_wake = true },
      { .name = "timer 2, clcd 3: timer is nearer", .vic_enable = T | C,
        .t4_state = TIMER4_STATE_START, .t4_count = 10u, .t4_value = 2u,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 1u,
        .expect_ticks = 2u, .expect_wake = true },
      { .name = "timer 5, tvout 2: tvout is nearer", .vic_enable = T | V,
        .t4_state = TIMER4_STATE_START, .t4_count = 5u, .t4_value = 5u,
        .tvout_running = true, .tvout_unmasked = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 5u,
        .expect_ticks = 2u, .expect_wake = true },
      { .name = "timer 1, tvout 2: timer is nearer", .vic_enable = T | V,
        .t4_state = TIMER4_STATE_START, .t4_count = 1u, .t4_value = 1u,
        .tvout_running = true, .tvout_unmasked = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 5u,
        .expect_ticks = 1u, .expect_wake = true },
      { .name = "all three live, clcd nearest", .vic_enable = T | C | V,
        .t4_state = TIMER4_STATE_START, .t4_count = 10u, .t4_value = 10u,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 2u,
        .tvout_running = true, .tvout_unmasked = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 2u,
        .expect_ticks = 2u, .expect_wake = true },
      { .name = "all three live, tvout nearest", .vic_enable = T | C | V,
        .t4_state = TIMER4_STATE_START, .t4_count = 10u, .t4_value = 10u,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 8u, .clcd_frame_accum = 0u,
        .tvout_running = true, .tvout_unmasked = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 4u,
        .expect_ticks = 3u, .expect_wake = true },
      { .name = "all three live, timer nearest", .vic_enable = T | C | V,
        .t4_state = TIMER4_STATE_START, .t4_count = 10u, .t4_value = 1u,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 8u, .clcd_frame_accum = 0u,
        .tvout_running = true, .tvout_unmasked = true,
        .tvout_frame_ticks = 7u, .tvout_frame_accum = 4u,
        .expect_ticks = 1u, .expect_wake = true },
      { .name = "timer and clcd tie at 4", .vic_enable = T | C,
        .t4_state = TIMER4_STATE_START, .t4_count = 4u, .t4_value = 4u,
        .clcd_running = true, .clcd_intmask = CLCD_INT_FRAME,
        .clcd_frame_ticks = 4u, .clcd_frame_accum = 0u,
        .expect_ticks = 4u, .expect_wake = true },
      /* Nothing enabled at all: the wait must not invent a boundary to run to,
       * and must not hang the host looking for one. */
      { .name = "no source enabled anywhere", .expect_ticks = 0u },
    };

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t ticks = 0xffffffffu;
        bool wake = false;
        wfi_edge_case_run(&cases[i], &ticks, &wake);
        CHECK(ticks == cases[i].expect_ticks && wake == cases[i].expect_wake,
              "%s: WFI covered %u ticks (wake=%d), expected %u (wake=%d)",
              cases[i].name, ticks, (int)wake,
              cases[i].expect_ticks, (int)cases[i].expect_wake);
    }
}

/* -------------------------------------------------------- wake sources ---
 *
 * The set of things that can end a WFI is data, so that a device modelled
 * tomorrow is observable the day it asserts its line rather than the day
 * somebody remembers to edit the idle path. These exercise the same reduction
 * the machine uses, with sources the machine does not have.
 *
 * Each stand-in answers a fixed distance so the arithmetic under test is the
 * reduction, not the device.
 */
static s5l_wake_kind_t wake_never(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; (void)ticks; return S5L_WAKE_NEVER;
}
static s5l_wake_kind_t wake_unknown(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; (void)ticks; return S5L_WAKE_UNKNOWN;
}
static s5l_wake_kind_t wake_at_3(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; *ticks = 3u; return S5L_WAKE_AT;
}
static s5l_wake_kind_t wake_at_9(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; *ticks = 9u; return S5L_WAKE_AT;
}
static s5l_wake_kind_t wake_at_20(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; *ticks = 20u; return S5L_WAKE_AT;
}
/* A distance of zero is not a moment still to come: every line was already
 * refreshed at zero elapsed time before the distance was asked for. */
static s5l_wake_kind_t wake_at_zero(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; *ticks = 0u; return S5L_WAKE_AT;
}

static void test_wfi_nearer_wake_source_wins(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 16), "machine init failed");

    /* Lines 4, 5 and 6 are routable and unclaimed by any modelled device. */
    const s5l_wake_source_t src[] = {
        { "far",     4u, wake_at_9  },
        { "near",    5u, wake_at_3  },
        { "farther", 6u, wake_at_20 },
    };
    m.vic[0].enable = (1u << 4) | (1u << 5) | (1u << 6);

    uint32_t at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, src, 3u, &at) == S5L_WAKE_AT && at == 3u,
          "nearest edge = %u, expected 3", at);

    /* Masking the nearest source's line hands the wait to the next-nearest:
     * a source that cannot reach the CPU cannot end the wait, however close. */
    m.vic[0].enable &= ~(1u << 5);
    at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, src, 3u, &at) == S5L_WAKE_AT && at == 9u,
          "with the near line masked the edge = %u, expected 9", at);

    /* A source on a VIC1 line is gated by VIC1's own enable. The chain this
     * replaced only ever read vic[0], so it could not have expressed a device
     * on any of the device tree's lines 32-63 — the watchdog, SDIO, GPIO. */
    const s5l_wake_source_t on_vic1[] = { { "vic1-dev", 40u, wake_at_3 } };
    at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, on_vic1, 1u, &at) == S5L_WAKE_NEVER &&
          at == 0xdeadbeefu,
          "a VIC1 source with its line disabled should not offer an edge (%u)",
          at);
    m.vic[1].enable = 1u << (40u - 32u);
    at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, on_vic1, 1u, &at) == S5L_WAKE_AT && at == 3u,
          "a VIC1 source with its line enabled gave %u, expected 3", at);

    /* A line outside the pair is one nothing can ever assert. */
    const s5l_wake_source_t unroutable[] = { { "line-64", 64u, wake_at_3 } };
    CHECK(s5l8900_next_wake(&m, unroutable, 1u, NULL) == S5L_WAKE_NEVER,
          "an unroutable line offered an edge");

    s5l8900_free(&m);
}

static void test_wfi_never_source_is_ignored_not_treated_as_zero(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 16), "machine init failed");
    m.vic[0].enable = (1u << 3) | (1u << 4) | (1u << 5);

    /* "Never" is an absence, not a distance. Folded in as zero it would pin
     * every wait at the current instant and turn WFI into a busy loop. */
    const s5l_wake_source_t mixed[] = {
        { "quiet-before", 3u, wake_never },
        { "live",         4u, wake_at_9  },
        { "quiet-after",  5u, wake_never },
    };
    uint32_t at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, mixed, 3u, &at) == S5L_WAKE_AT && at == 9u,
          "a quiet source displaced the live edge: %u, expected 9", at);

    /* With every source quiet there is no edge at all — and the distance is
     * left untouched rather than reported as zero ticks away. */
    const s5l_wake_source_t all_quiet[] = {
        { "quiet-a", 3u, wake_never },
        { "quiet-b", 4u, wake_never },
    };
    at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, all_quiet, 2u, &at) == S5L_WAKE_NEVER &&
          at == 0xdeadbeefu,
          "all-quiet reported kind=%d ticks=%u, expected NEVER and no distance",
          (int)s5l8900_next_wake(&m, all_quiet, 2u, NULL), at);

    s5l8900_free(&m);
}

static void test_wfi_unpredictable_source_stops_the_fast_forward(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 16), "machine init failed");
    m.vic[0].enable = (1u << 3) | (1u << 4) | (1u << 5);

    const s5l_wake_source_t alone[] = { { "vague", 3u, wake_unknown } };
    CHECK(s5l8900_next_wake(&m, alone, 1u, NULL) == S5L_WAKE_UNKNOWN,
          "a source that cannot say when it fires must stop the skip");

    /* An unknown outranks a known edge. Running to the known one could carry
     * guest time straight over the moment the vague source fired. */
    const s5l_wake_source_t with_known[] = {
        { "vague", 3u, wake_unknown },
        { "near",  4u, wake_at_3    },
    };
    uint32_t at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, with_known, 2u, &at) == S5L_WAKE_UNKNOWN &&
          at == 0xdeadbeefu,
          "a known edge overrode an unknown one: kind=%d ticks=%u",
          (int)s5l8900_next_wake(&m, with_known, 2u, NULL), at);

    /* Zero is not a future distance, and a declared source with no callback
     * is a source that has not said anything. Both fail safe. */
    const s5l_wake_source_t zero_distance[] = { { "now", 3u, wake_at_zero } };
    CHECK(s5l8900_next_wake(&m, zero_distance, 1u, NULL) == S5L_WAKE_UNKNOWN,
          "a zero distance was accepted as an edge");
    const s5l_wake_source_t no_callback[] = { { "silent", 3u, NULL } };
    CHECK(s5l8900_next_wake(&m, no_callback, 1u, NULL) == S5L_WAKE_UNKNOWN,
          "a source with no next_edge was accepted");

    /* But an unknown behind a masked line still cannot reach the CPU, so it
     * does not get to hold the whole machine back. */
    const s5l_wake_source_t masked_vague[] = {
        { "vague", 9u, wake_unknown },     /* line 9 is not enabled above */
        { "near",  4u, wake_at_9    },
    };
    at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, masked_vague, 2u, &at) == S5L_WAKE_AT &&
          at == 9u,
          "a masked unknown blocked the skip anyway: %u", at);

    /* Degenerate inputs decline rather than guess. An empty table is the one
     * that says NEVER: nothing declared, so nothing to wait for. */
    CHECK(s5l8900_next_wake(&m, NULL, 0u, NULL) == S5L_WAKE_NEVER,
          "an empty source table should offer no edge");
    CHECK(s5l8900_next_wake(&m, NULL, 2u, NULL) == S5L_WAKE_UNKNOWN,
          "a null table with a non-zero count should fail safe");
    CHECK(s5l8900_next_wake(NULL, alone, 1u, NULL) == S5L_WAKE_UNKNOWN,
          "a null machine should fail safe");
    s5l8900_free(&m);

    /*
     * End to end: when nothing can name an edge the machine must not fast
     * forward, at all, and must not hang the host looking for one. The clocks
     * are left at the guest's real 412 MHz : 6 MHz here so that a wrongly
     * unbounded wait would show up as a large jump in guest time.
     */
    s5l8900_t idle;
    CHECK(s5l8900_init(&idle, 0, 1u << 16), "machine init failed");
    const uint32_t program[] = { 0xee070f90u, 0xee070f90u, 0xe3a02007u };
    s5l8900_load(&idle, 0, program, sizeof program);
    idle.timer.t4_state = TIMER4_STATE_START;      /* armed, but VIC-masked */
    idle.timer.t4_count = idle.timer.t4_value = 9u;
    idle.cpu.cpsr = ARM_MODE_SYS;
    for (unsigned i = 0; i < 2u; i++) {
        arm_status_t st = arm_step(&idle.cpu);
        CHECK(st == ARM_OK && idle.cpu.r[15] == 4u * (i + 1u),
              "idle WFI %u: status=%d pc=%08x", i, (int)st, idle.cpu.r[15]);
    }
    CHECK(idle.timer.ticks == 0u && idle.tb_accum == 0u &&
          idle.timer.t4_value == 9u,
          "an unpredictable machine still skipped time: ticks=%llu frac=%llu",
          (unsigned long long)idle.timer.ticks,
          (unsigned long long)idle.tb_accum);
    arm_status_t st = arm_step(&idle.cpu);
    CHECK(st == ARM_OK && idle.cpu.r[2] == 7u,
          "host made no progress past the idle WFIs: status=%d r2=%u",
          (int)st, idle.cpu.r[2]);
    s5l8900_free(&idle);
}

/* Every ordering of the same sources must reduce to the same answer: the wait
 * takes a minimum, and a minimum has no order. */
static void permute_wake_check(const s5l8900_t *m, const s5l_wake_source_t *src,
                               unsigned n, unsigned *order, bool *used,
                               unsigned k, s5l_wake_kind_t want,
                               uint32_t want_ticks, const char *what) {
    /* n! permutations, into fixed buffers. Six is already 720 orderings; a
     * table that outgrows this wants sampling rather than exhaustion. */
    if (n > 6u) {
        CHECK(false, "%s: %u sources is too many to permute exhaustively",
              what, n);
        return;
    }
    if (k == n) {
        s5l_wake_source_t shuffled[8];
        char shown[16];
        unsigned p = 0;
        for (unsigned i = 0; i < n; i++) {
            shuffled[i] = src[order[i]];
            if (p + 1u < sizeof shown) shown[p++] = (char)('0' + order[i]);
        }
        shown[p] = '\0';
        uint32_t at = 0xdeadbeefu;
        s5l_wake_kind_t got = s5l8900_next_wake(m, shuffled, n, &at);
        CHECK(got == want &&
              (want != S5L_WAKE_AT || at == want_ticks),
              "%s: order %s gave kind=%d ticks=%u, expected kind=%d ticks=%u",
              what, shown, (int)got, at, (int)want, want_ticks);
        return;
    }
    for (unsigned i = 0; i < n; i++) {
        if (used[i]) continue;
        used[i] = true;
        order[k] = i;
        permute_wake_check(m, src, n, order, used, k + 1u, want, want_ticks,
                           what);
        used[i] = false;
    }
}

static void test_wfi_wake_source_order_does_not_matter(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 16), "machine init failed");
    m.vic[0].enable = (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6);
    unsigned order[8];
    bool used[8] = { false, false, false, false, false, false, false, false };

    const s5l_wake_source_t four[] = {
        { "quiet",   3u, wake_never },
        { "far",     4u, wake_at_9  },
        { "near",    5u, wake_at_3  },
        { "farther", 6u, wake_at_20 },
    };
    permute_wake_check(&m, four, 4u, order, used, 0u, S5L_WAKE_AT, 3u,
                       "nearest of four");

    /* An unknown dominates from any position, not just from the front. */
    const s5l_wake_source_t vague[] = {
        { "quiet", 3u, wake_never   },
        { "far",   4u, wake_at_9    },
        { "vague", 5u, wake_unknown },
        { "near",  6u, wake_at_3    },
    };
    permute_wake_check(&m, vague, 4u, order, used, 0u, S5L_WAKE_UNKNOWN, 0u,
                       "unknown among four");

    /*
     * And the machine's own table, permuted, against a live state where each
     * of the three has a different distance: timer 10, CLCD 2, TV-out 5.
     * This also proves the table itself is well formed — every entry routable
     * and answerable — which a bad new entry would break here rather than in a
     * boot.
     */
    const s5l_wake_source_t *real = NULL;
    unsigned nreal = s5l8900_wake_sources(&real);
    /* timer, clcd, tvout, spi0, spi1, uart4-rx, dmac0, dmac1, and one per GPIO
     * interrupt group. The GPIO seven are declared together because
     * /arm-io/gpio's `interrupts` array is descending and declaring only the
     * group that carries touch would make the whole test depend on that
     * transcription being right. uart4-rx joined them with the receive path: it
     * is the only source in the table whose producer is OUTSIDE the machine.
     * The two DMA controllers joined with the PL080 model; both answer NEVER
     * today for the reason the SPI pair does, and both are declared anyway
     * because this table is the machine's definition of what can interrupt it. */
    CHECK(real != NULL && nreal == 8u + S5L_GPIOIC_GROUPS,
          "the machine declares %u wake sources, expected %u",
          nreal, 8u + S5L_GPIOIC_GROUPS);
    for (unsigned i = 0; i < nreal; i++)
        CHECK(real[i].name && real[i].next_edge &&
              real[i].line < 32u * S5L8900_VIC_COUNT,
              "wake source %u is malformed: name=%s line=%u",
              i, real[i].name ? real[i].name : "(null)", real[i].line);

    m.vic[0].enable = (1u << S5L8900_IRQ_TIMER) | (1u << S5L8900_IRQ_CLCD) |
                      (1u << S5L8900_IRQ_TVOUT);
    m.timer.t4_state = TIMER4_STATE_START;
    m.timer.t4_count = m.timer.t4_value = 10u;
    m.clcd.scanning = true;
    m.clcd.ctrl = CLCD_CTRL_ENABLE;
    m.clcd.gate = 1u;
    m.clcd.intmask = CLCD_INT_FRAME;
    m.clcd.frame_ticks = 6u;
    m.clcd.frame_accum = 4u;
    m.tvout.regs[S5L_TVOUT_BANK_MIXER][0] = TVOUT_RUN;
    m.tvout.regs[S5L_TVOUT_BANK_SDO][0]   = TVOUT_RUN;
    m.tvout.regs[S5L_TVOUT_BANK_SDO][TVOUT_SDO_IRQMASK / 4u] = 0u;
    m.tvout.frame_ticks = 9u;
    m.tvout.frame_accum = 4u;

    uint32_t at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, real, nreal, &at) == S5L_WAKE_AT && at == 2u,
          "the machine's own table gave %u, expected the CLCD's 2", at);
    /*
     * Permute only the sources whose lines are actually enabled here. The
     * reduction skips a masked source before it asks anything, so a masked
     * entry's position in the array cannot affect the answer and permuting it
     * adds a factorial for nothing — and the table is twelve entries now,
     * which is 479 million orderings. The three enabled ones are the three
     * with distinct distances, which is the property being tested.
     */
    s5l_wake_source_t live[8];
    unsigned nlive = 0;
    for (unsigned i = 0; i < nreal && nlive < 8u; i++) {
        unsigned line = real[i].line;
        if (line >= 32u * S5L8900_VIC_COUNT) continue;
        if (!(m.vic[line / 32u].enable & (1u << (line % 32u)))) continue;
        live[nlive++] = real[i];
    }
    CHECK(nlive == 3u, "%u of the machine's sources are enabled, expected the "
          "timer, the CLCD and TV-out", nlive);
    permute_wake_check(&m, live, nlive, order, used, 0u, S5L_WAKE_AT, 2u,
                       "the machine's own enabled sources");
    s5l8900_free(&m);
}

/*
 * Guest time must advance at the guest's own CPU:timebase ratio, not once per
 * instruction. Running the timebase at instruction rate makes time pass ~68x
 * too fast relative to work done, so the kernel never finishes servicing one
 * decrementer deadline before the next is already past; it clamps to its
 * minimum and re-enters forever. That livelock burned 66% of a 200M-instruction
 * boot inside the FIQ handler before this ratio existed.
 */
static void test_timebase_runs_at_the_guest_ratio(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    const uint32_t insns = 412000;      /* one hundredth of a guest second */
    for (uint32_t i = 0; i < insns; i++) s5l8900_tick(&m, 1);

    uint32_t tb = m.bus.read32(m.bus.ctx, S5L8900_TIMER_BASE + TIMER_TICKSLOW);
    uint32_t want = (uint32_t)((uint64_t)insns * S5L8900_TB_HZ / S5L8900_CPU_HZ);
    CHECK(tb == want, "timebase=%u expect %u (%u insns at %u:%u)",
          tb, want, insns, S5L8900_CPU_HZ, S5L8900_TB_HZ);
    CHECK(tb != insns, "timebase must not advance once per instruction");

    /* The remainder must carry rather than be discarded: ticking one at a time
     * has to match ticking in one lump, or time drifts against itself. */
    s5l8900_t b;
    CHECK(s5l8900_init(&b, 0, 1u << 20), "machine init failed");
    s5l8900_tick(&b, insns);
    uint32_t lump = b.bus.read32(b.bus.ctx, S5L8900_TIMER_BASE + TIMER_TICKSLOW);
    CHECK(lump == tb, "lump=%u single-stepped=%u — remainder is being dropped",
          lump, tb);

    s5l8900_free(&m);
    s5l8900_free(&b);
}

/*
 * The per-instruction tick skips its level refresh when no timebase tick
 * elapsed and nothing has been touched (see `level_dirty` in soc.h). That skip
 * took tools/insnbench's slowest tick=yes row from 1.58 to 7.89 M guest
 * instructions per second, and it is only allowed to exist because it changes
 * nothing the guest can observe -- so this runs the two paths side by side and
 * compares them instruction by instruction.
 *
 * `reference` is forced through the full refresh by setting the flag itself
 * before every tick, which is exactly what the code did before the skip.
 * Nothing here is a claim about a real boot -- that is the byte-identical
 * snapshot comparison the change was accepted on -- but a divergence in these
 * few thousand ticks would be one that never had to be reproduced.
 */
static void machine_visible_state(const s5l8900_t *m, uint32_t *out) {
    unsigned n = 0;
    out[n++] = m->cpu.irq_line ? 1u : 0u;
    out[n++] = m->cpu.fiq_line ? 1u : 0u;
    for (unsigned i = 0; i < S5L8900_VIC_COUNT; i++) out[n++] = m->vic[i].raw;
    out[n++] = m->timer.ticks;
    out[n++] = m->timer.t4_value;
    out[n++] = m->timer.irqlatch;
    out[n++] = m->clcd.intstatus;
    out[n++] = m->clcd.frame_accum;
    out[n++] = (uint32_t)m->clcd.frames;
    out[n++] = m->tvout.frame_accum;
    out[n++] = (uint32_t)m->pmu.seconds;
    out[n++] = (uint32_t)m->pmu.tick_accum;
    out[n++] = (uint32_t)m->tb_accum;
    for (unsigned g = 0; g < S5L_GPIOIC_GROUPS; g++) {
        out[n++] = m->gpioic.stat[g];
        out[n++] = m->gpioic.raw[g];
    }
}
#define VISIBLE_WORDS (12u + S5L8900_VIC_COUNT + 2u * S5L_GPIOIC_GROUPS)

static void arm_a_live_machine(s5l8900_t *m) {
    /* A timer that expires inside the loop, a CLCD scanning, and both lines
     * enabled -- so the comparison covers an interrupt actually arriving and
     * not just two idle machines agreeing about nothing. */
    m->bus.write32(m->bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE,
                   (1u << S5L8900_IRQ_TIMER) | (1u << S5L8900_IRQ_CLCD));
    m->bus.write32(m->bus.ctx, S5L8900_TIMER_BASE + TIMER4_COUNTBUF, 7u);
    m->bus.write32(m->bus.ctx, S5L8900_TIMER_BASE + TIMER4_STATE,
                   TIMER4_STATE_START | TIMER4_STATE_UPDATE);
    m->bus.write32(m->bus.ctx, S5L8900_CLCD_BASE + CLCD_INTMASK, CLCD_INT_FRAME);
    m->bus.write32(m->bus.ctx, S5L8900_CLCD_BASE + CLCD_CTRL, CLCD_CTRL_ENABLE);
    m->bus.write32(m->bus.ctx, S5L8900_CLCD_BASE + CLCD_GATE, 1u);
    m->bus.write32(m->bus.ctx, S5L8900_CLCD_BASE + CLCD_ENABLE, 1u);
    m->clcd.frame_ticks = 11u;      /* frames inside the loop, not every 60th s */
}

/* Clear whatever is asserting, through the bus, as the kernel's own handlers
 * do. Returns 1 if there was anything to clear. */
static unsigned ack_pending(s5l8900_t *m) {
    if (!m->cpu.irq_line && !m->cpu.fiq_line) return 0u;
    m->bus.write32(m->bus.ctx, S5L8900_TIMER_BASE + TIMER_IRQACK,
                   TIMER4_IRQ_BITS);
    m->bus.write32(m->bus.ctx, S5L8900_CLCD_BASE + CLCD_INTSTATUS,
                   CLCD_INT_FRAME);
    return 1u;
}

static void test_skipped_refresh_is_invisible_to_the_guest(void) {
    s5l8900_t fast, reference;
    CHECK(s5l8900_init(&fast, 0, 1u << 20), "machine init failed");
    CHECK(s5l8900_init(&reference, 0, 1u << 20), "machine init failed");
    arm_a_live_machine(&fast);
    arm_a_live_machine(&reference);

    uint32_t a[VISIBLE_WORDS], b[VISIBLE_WORDS];
    unsigned diverged = 0, acks = 0;
    const unsigned TICKS = 200000u;         /* ~2900 timebase ticks */
    for (unsigned i = 0; i < TICKS && !diverged; i++) {
        reference.level_dirty = true;       /* the pre-skip code path, exactly */
        s5l8900_tick(&reference, 1u);
        s5l8900_tick(&fast, 1u);

        /* The guest reads its own hardware occasionally, not every step: an
         * access forces the refresh, so a loop that read every tick would test
         * the fast path by never taking it. */
        if ((i % 397u) == 0u) {
            uint32_t ra = fast.bus.read32(fast.bus.ctx,
                              S5L8900_TIMER_BASE + TIMER_TICKSLOW);
            uint32_t rb = reference.bus.read32(reference.bus.ctx,
                              S5L8900_TIMER_BASE + TIMER_TICKSLOW);
            if (ra != rb) { diverged = i + 1u; break; }
            ra = fast.bus.read32(fast.bus.ctx, S5L8900_VIC0_BASE + VIC_RAWINTR);
            rb = reference.bus.read32(reference.bus.ctx,
                                      S5L8900_VIC0_BASE + VIC_RAWINTR);
            if (ra != rb) { diverged = i + 1u; break; }
        }
        /*
         * And acknowledges, the way the handlers do -- each machine on its own
         * line, so a line that came up at the wrong tick produces a different
         * acknowledge and the comparison below sees it. Both sources, because
         * a latch nobody clears stays high, and a machine whose line never
         * drops does MMIO on every tick and therefore never skips.
         */
        acks += ack_pending(&fast);
        (void)ack_pending(&reference);

        machine_visible_state(&fast, a);
        machine_visible_state(&reference, b);
        if (memcmp(a, b, sizeof a) != 0) diverged = i + 1u;
    }

    CHECK(!diverged, "the skipping and refreshing machines differ at tick %u",
          diverged);
    /* Evidence the run was not two idle machines agreeing about nothing. */
    CHECK(acks > 100u, "only %u interrupts in %u ticks — the devices never "
          "fired, so nothing was compared", acks, TICKS);
    CHECK(fast.timer.ticks > 0u && fast.clcd.frames > 0u,
          "timebase=%llu frames=%llu — no device advanced",
          (unsigned long long)fast.timer.ticks,
          (unsigned long long)fast.clcd.frames);

    s5l8900_free(&fast);
    s5l8900_free(&reference);
}

/*
 * s5l8900_run() calls the now-small public converter with a constant one-
 * retirement tick, allowing an optimizing compiler to inline the conversion
 * and early-out while leaving the large refresh body out of line. This is an
 * equivalence test, not merely a timer spot-check: run that API beside the
 * literal public arm_step()+s5l8900_tick(1) contract and compare the CPU,
 * clock phase, interrupt-visible state, and every device whose state can
 * advance in a refresh.
 *
 * Privileged and User-mode normal and fractional phases exercise tick
 * batching; 1:1 proves an edge cannot be crossed; guest
 * MMIO, a user SVC, and a pre-step hook prove all three immediate escape gates.
 * The direct button change is the host-behind-the-bus case ext_inputs() watches.
 * Inverted/zero rates and an invalid accumulator pin the public API's general
 * semantics instead of letting a benchmark-only assumption become behavior.
 */
struct run_tick_case {
    const char *name;
    uint32_t cpu_hz, tb_hz;
    uint64_t tb_accum;
    unsigned steps;
    bool user_mode;
    bool dirty_mmio;
    bool external_input;
    bool mode_escape;
    bool pre_step_hook;
    bool expect_batch;
};

static bool never_handle_pre_step(void *opaque) {
    (void)opaque;
    return false;
}

static bool setup_run_tick_case(s5l8900_t *m, const struct run_tick_case *tc) {
    if (!s5l8900_init(m, 0, 1u << 20)) return false;

    static const uint32_t spin = 0xeafffffeu;       /* B . */
    static const uint32_t dirty_loop[] = {
        0xe5801000u,                               /* STR r1,[r0] */
        0xeafffffdu                                /* B 0         */
    };
    static const uint32_t user_svc_then_spin[] = {
        0xef000000u,                               /* SVC 0       */
        0xeafffffeu,                               /* B .         */
        0xeafffffeu                                /* vector 0x08 */
    };
    if (tc->mode_escape) {
        s5l8900_load(m, 0, user_svc_then_spin, sizeof user_svc_then_spin);
    } else if (tc->dirty_mmio) {
        s5l8900_load(m, 0, dirty_loop, sizeof dirty_loop);
        m->cpu.r[0] = S5L8900_VIC0_BASE + VIC_SOFTINT;
        m->cpu.r[1] = 1u << 3;
    } else {
        s5l8900_load(m, 0, &spin, sizeof spin);
    }

    arm_a_live_machine(m);
    m->cpu_hz = tc->cpu_hz;
    m->tb_hz = tc->tb_hz;
    /* Start from a genuinely eligible clean boundary. Otherwise init's
     * mandatory first refresh would make a mode-changing first instruction
     * take the fallback path and leave the escape logic untested. */
    s5l8900_tick(m, 0u);
    m->tb_accum = tc->tb_accum;
    m->cpu.r[15] = 0u;
    arm_set_mode(&m->cpu, tc->user_mode ? ARM_MODE_USR : ARM_MODE_SVC);
    m->cpu.cpsr |= ARM_CPSR_I | ARM_CPSR_F;

    /* Deliberately bypass the bus: this models the host changing the board
     * through the s5l_buttons_t pointer it is publicly handed. The next tick
     * must notice it even when no guest MMIO made level_dirty true. */
    if (tc->external_input)
        m->buttons.pressed = (uint8_t)(1u << S5L_BUTTON_MENU);
    if (tc->pre_step_hook) {
        const uint32_t target = 0u;
        if (!s5l8900_set_pre_step_hook(m, never_handle_pre_step, NULL,
                                      &target, 1u)) {
            s5l8900_free(m);
            return false;
        }
    }
    return true;
}

static void test_run_tick_path_matches_public_contract(void) {
    static const struct run_tick_case cases[] = {
        { .name = "privileged control", .cpu_hz = S5L8900_CPU_HZ,
          .tb_hz = S5L8900_TB_HZ, .steps = 1000u, .expect_batch = true },
        { .name = "user real ratio", .cpu_hz = S5L8900_CPU_HZ,
          .tb_hz = S5L8900_TB_HZ, .steps = 1000u,
          .user_mode = true, .expect_batch = true },
        { .name = "user fractional phase", .cpu_hz = 4u, .tb_hz = 1u,
          .tb_accum = 3u, .steps = 257u,
          .user_mode = true, .expect_batch = true },
        { .name = "user one to one", .cpu_hz = 1u, .tb_hz = 1u,
          .steps = 257u, .user_mode = true },
        { .name = "user dirty guest MMIO", .cpu_hz = 4u, .tb_hz = 1u,
          .tb_accum = 2u, .steps = 258u, .user_mode = true,
          .dirty_mmio = true, .expect_batch = true },
        { .name = "user external button gate", .cpu_hz = 4u, .tb_hz = 1u,
          .tb_accum = 2u, .steps = 1u, .user_mode = true,
          .external_input = true },
        { .name = "user SVC escape", .cpu_hz = S5L8900_CPU_HZ,
          .tb_hz = S5L8900_TB_HZ, .steps = 17u, .user_mode = true,
          .mode_escape = true, .expect_batch = true },
        { .name = "user pre-step gate", .cpu_hz = S5L8900_CPU_HZ,
          .tb_hz = S5L8900_TB_HZ, .steps = 257u, .user_mode = true,
          .pre_step_hook = true },
        { .name = "user inverted fallback", .cpu_hz = 1u, .tb_hz = 4u,
          .steps = 257u, .user_mode = true },
        { .name = "user zero CPU fallback", .tb_hz = 1u,
          .tb_accum = 7u, .steps = 257u, .user_mode = true },
        { .name = "user zero timebase fallback", .cpu_hz = 4u,
          .tb_accum = 3u, .steps = 257u, .user_mode = true },
        { .name = "user invalid phase first-step gate", .cpu_hz = 4u,
          .tb_hz = 1u, .tb_accum = 5u, .steps = 1u, .user_mode = true },
        { .name = "user invalid phase recovery", .cpu_hz = 4u, .tb_hz = 1u,
          .tb_accum = 5u, .steps = 257u, .user_mode = true,
          .expect_batch = true }
    };

    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        const struct run_tick_case *tc = &cases[c];
        s5l8900_t fast, reference;
        bool fast_ok = setup_run_tick_case(&fast, tc);
        bool reference_ok = setup_run_tick_case(&reference, tc);
        CHECK(fast_ok && reference_ok, "%s: machine init failed", tc->name);
        if (!fast_ok || !reference_ok) {
            if (fast_ok) s5l8900_free(&fast);
            if (reference_ok) s5l8900_free(&reference);
            continue;
        }

        arm_status_t reference_status = ARM_OK;
        unsigned reference_ran = 0u;
        for (; reference_ran < tc->steps; reference_ran++) {
            reference_status = arm_step(&reference.cpu);
            if (reference_status != ARM_OK) break;
            s5l8900_tick(&reference, 1u);
        }

        arm_status_t fast_status = ARM_OK;
        unsigned fast_ran = s5l8900_run(&fast, tc->steps, &fast_status);
        uint32_t a[VISIBLE_WORDS], b[VISIBLE_WORDS];
        machine_visible_state(&fast, a);
        machine_visible_state(&reference, b);

        CHECK(fast_status == reference_status && fast_ran == reference_ran,
              "%s: status/ran fast=%d/%u reference=%d/%u", tc->name,
              (int)fast_status, fast_ran, (int)reference_status, reference_ran);
        CHECK(fast.cpu.r[15] == reference.cpu.r[15] &&
              fast.cpu.cpsr == reference.cpu.cpsr &&
              fast.cpu.cycles == reference.cpu.cycles,
              "%s: CPU diverged pc=%08x/%08x cpsr=%08x/%08x cycles=%llu/%llu",
              tc->name, fast.cpu.r[15], reference.cpu.r[15], fast.cpu.cpsr,
              reference.cpu.cpsr, (unsigned long long)fast.cpu.cycles,
              (unsigned long long)reference.cpu.cycles);
        CHECK(fast.tb_accum == reference.tb_accum &&
              fast.level_dirty == reference.level_dirty &&
              fast.ext_seen == reference.ext_seen,
              "%s: converter/refresh phase differs accum=%llu/%llu dirty=%d/%d "
              "external=%08x/%08x", tc->name,
              (unsigned long long)fast.tb_accum,
              (unsigned long long)reference.tb_accum,
              (int)fast.level_dirty, (int)reference.level_dirty,
              fast.ext_seen, reference.ext_seen);
        CHECK(memcmp(a, b, sizeof a) == 0,
              "%s: guest-visible machine state diverged", tc->name);
        CHECK(memcmp(&fast.timer, &reference.timer, sizeof fast.timer) == 0 &&
              memcmp(&fast.clcd, &reference.clcd, sizeof fast.clcd) == 0 &&
              memcmp(&fast.tvout, &reference.tvout, sizeof fast.tvout) == 0 &&
              memcmp(&fast.vic, &reference.vic, sizeof fast.vic) == 0 &&
              memcmp(&fast.gpioic, &reference.gpioic, sizeof fast.gpioic) == 0 &&
              memcmp(&fast.buttons, &reference.buttons, sizeof fast.buttons) == 0,
              "%s: refreshed device state diverged", tc->name);

        uint64_t batches = s5l8900_interpreter_tick_batches(&fast);
        uint64_t batched_retired =
            s5l8900_interpreter_tick_batched_retired(&fast);
        if (tc->expect_batch) {
            CHECK(batches > 0u && batched_retired >= batches,
                  "%s: batching path was not covered (batches=%llu retired=%llu)",
                  tc->name, (unsigned long long)batches,
                  (unsigned long long)batched_retired);
        } else {
            CHECK(batches == 0u && batched_retired == 0u,
                  "%s: ineligible path batched (batches=%llu retired=%llu)",
                  tc->name, (unsigned long long)batches,
                  (unsigned long long)batched_retired);
        }
        if (tc->mode_escape)
            CHECK(batches == 1u && batched_retired == tc->steps,
                  "%s: SVC batch accounting changed unexpectedly "
                  "(batches=%llu retired=%llu)", tc->name,
                  (unsigned long long)batches,
                  (unsigned long long)batched_retired);

        s5l8900_free(&fast);
        s5l8900_free(&reference);
    }
}

static void test_privileged_batch_flushes_before_wfi(void) {
    enum { NOPS = 3, TIMER_TICKS = 4 };
    const uint32_t program[NOPS + 1] = {
        0xe1a00000u,                    /* mov r0,r0 */
        0xe1a00000u,                    /* mov r0,r0 */
        0xe1a00000u,                    /* mov r0,r0 */
        0xe320f003u                     /* WFI */
    };
    s5l8900_t fast, reference;
    bool fast_ok = s5l8900_init(&fast, 0u, 1u << 20);
    bool reference_ok = s5l8900_init(&reference, 0u, 1u << 20);
    CHECK(fast_ok && reference_ok, "privileged WFI machine init failed");
    if (!fast_ok || !reference_ok) {
        if (fast_ok) s5l8900_free(&fast);
        if (reference_ok) s5l8900_free(&reference);
        return;
    }

    s5l8900_t *machines[] = { &fast, &reference };
    for (unsigned i = 0; i < sizeof machines / sizeof machines[0]; i++) {
        s5l8900_t *m = machines[i];
        s5l8900_load(m, 0u, program, sizeof program);
        m->cpu_hz = 4u;
        m->tb_hz = 1u;
        m->cpu.r[15] = 0u;
        m->cpu.cpsr = ARM_MODE_SVC | ARM_CPSR_I | ARM_CPSR_F;
        m->bus.write32(m->bus.ctx, S5L8900_TIMER_BASE + TIMER4_COUNTBUF,
                       TIMER_TICKS);
        m->bus.write32(m->bus.ctx, S5L8900_TIMER_BASE + TIMER4_STATE,
                       TIMER4_STATE_START | TIMER4_STATE_UPDATE);
        m->bus.write32(m->bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE,
                       1u << S5L8900_IRQ_TIMER);
        s5l8900_tick(m, 0u);
    }

    arm_status_t fast_status = ARM_OK, reference_status = ARM_OK;
    unsigned fast_ran = s5l8900_run(&fast, NOPS + 1u, &fast_status);
    unsigned reference_ran = 0u;
    for (; reference_ran < NOPS + 1u; reference_ran++) {
        reference_status = arm_step(&reference.cpu);
        if (reference_status != ARM_OK) break;
        s5l8900_tick(&reference, 1u);
    }

    CHECK(fast_status == reference_status && fast_ran == reference_ran,
          "privileged WFI status/ran fast=%d/%u reference=%d/%u",
          (int)fast_status, fast_ran, (int)reference_status, reference_ran);
    CHECK(fast.timer.ticks == reference.timer.ticks &&
          fast.timer.irqlatch == reference.timer.irqlatch &&
          fast.cpu.cycles == reference.cpu.cycles &&
          fast.cpu.r[15] == reference.cpu.r[15],
          "privileged WFI diverged ticks=%llu/%llu latch=%08x/%08x "
          "cycles=%llu/%llu pc=%08x/%08x",
          (unsigned long long)fast.timer.ticks,
          (unsigned long long)reference.timer.ticks,
          fast.timer.irqlatch, reference.timer.irqlatch,
          (unsigned long long)fast.cpu.cycles,
          (unsigned long long)reference.cpu.cycles,
          fast.cpu.r[15], reference.cpu.r[15]);
    CHECK(fast.interpreter_tick_batches > 0u &&
          fast.interpreter_tick_batched_retired >= NOPS,
          "privileged WFI did not exercise batching: batches=%llu retired=%llu",
          (unsigned long long)fast.interpreter_tick_batches,
          (unsigned long long)fast.interpreter_tick_batched_retired);
    CHECK(fast.batch_pending == 0u,
          "batch_pending leaked across run call: %u", fast.batch_pending);

    s5l8900_free(&fast);
    s5l8900_free(&reference);
}

/*
 * The device tree exposes /arm-io/mbx as one 16 MiB aperture: 8 KiB of
 * registers followed by EDRAM. The bus already decodes that whole span, so the
 * machine's window inventory, conflict detector, allocation and teardown must
 * describe the same object. A shorter advertised window is not cosmetic: it
 * permits a RAM/stub declaration to shadow live EDRAM, while a missing free
 * leaks almost 16 MiB for every machine a test constructs.
 */
static void test_mbx_edram_owns_and_declares_the_full_aperture(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    CHECK(m.mbx.edram != NULL, "machine init left the MBX EDRAM unallocated");
    if (!m.mbx.edram) { s5l8900_free(&m); return; }

    const uint32_t first = S5L8900_MBX_BASE + S5L_MBX_SIZE;
    const uint32_t last = S5L8900_MBX_BASE + S5L_MBX_APERTURE - 4u;
    uint64_t ur = m.unmapped_reads, uw = m.unmapped_writes;
    m.bus.write32(m.bus.ctx, first, 0x11223344u);
    m.bus.write32(m.bus.ctx, last, 0xa5c35a7eu);
    CHECK(m.bus.read32(m.bus.ctx, first) == 0x11223344u &&
          m.bus.read32(m.bus.ctx, last) == 0xa5c35a7eu,
          "the first/last EDRAM words did not round-trip");
    CHECK(m.unmapped_reads == ur && m.unmapped_writes == uw,
          "in-aperture EDRAM traffic was counted as unmapped");

    s5l_window_t windows[S5L_WINDOW_MAX];
    unsigned nw = s5l8900_windows(&m, windows, S5L_WINDOW_MAX);
    const s5l_window_t *mbx = NULL;
    for (unsigned i = 0; i < nw && i < S5L_WINDOW_MAX; i++)
        if (windows[i].base == S5L8900_MBX_BASE) mbx = &windows[i];
    CHECK(mbx && mbx->size == S5L_MBX_APERTURE &&
          strcmp(mbx->name, "mbx") == 0,
          "window inventory does not declare the full MBX aperture");

    const s5l_window_t *conflict = s5l8900_ram_conflict(last, 4u);
    CHECK(conflict && strcmp(conflict->name, "mbx") == 0,
          "RAM conflict detection permits the high end of MBX EDRAM");
    CHECK(!s5l8900_add_stub(&m, last, 4u, "over-mbx-edram"),
          "a stub was allowed to shadow the high end of MBX EDRAM");

    uint8_t *allocation = m.mbx.edram;
    s5l_mbx_reset(&m.mbx);
    CHECK(m.mbx.edram == allocation,
          "device reset dropped or replaced the machine-owned EDRAM");
    CHECK(m.bus.read32(m.bus.ctx, first) == 0u &&
          m.bus.read32(m.bus.ctx, last) == 0u,
          "device reset did not clear the EDRAM contents");

    s5l8900_free(&m);
    CHECK(m.mbx.edram == NULL, "machine teardown retained the EDRAM pointer");
}

/*
 * Stub windows are honest storage, not invented behaviour. The properties that
 * matter are that they read back what was written (rather than the 0 that an
 * unmapped read returns, which is what made a driver spin 3.9 million times),
 * that they are counted rather than silent, and above all that one can never
 * shadow a real device model.
 */
static void test_stub_window_stores_and_counts(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    /* 0x3b000000 used to be the address here, chosen because nothing
     * modelled it. The PowerVR MBX block does now, and add_stub
     * correctly refuses to shadow a device window, so this moved to an
     * address that is still unclaimed -- above every S5L8900_*_BASE. */
    CHECK(s5l8900_add_stub(&m, 0x3f800000u, 0x1000u, "unknown-3f8"),
          "declaring a stub window should succeed");
    /* The machine declares its own stubs at init, so this one is not index 0.
     * Look it up by name rather than by position — an index that silently
     * points at the wrong window would make every assertion below vacuous. */
    s5l_stub_t *mine = NULL;
    for (unsigned i = 0; i < m.stub_count; i++)
        if (!strcmp(m.stubs[i].name, "unknown-3f8")) mine = &m.stubs[i];
    CHECK(mine != NULL, "the stub just declared should be in the table");
    if (!mine) { s5l8900_free(&m); return; }

    /* Reads return what was written, not zero. */
    m.bus.write32(m.bus.ctx, 0x3f800010u, 0xa5a5a5a5u);
    CHECK(m.bus.read32(m.bus.ctx, 0x3f800010u) == 0xa5a5a5a5u,
          "stub read=%08x expect the value written",
          m.bus.read32(m.bus.ctx, 0x3f800010u));

    /* Stubbed traffic must not be counted as unmapped — it is accounted to the
     * window so it shows up by name in the report. */
    CHECK(m.unmapped_reads == 0 && m.unmapped_writes == 0,
          "stubbed access must not count as unmapped");
    CHECK(mine->reads == 1 && mine->writes == 1,
          "stub r=%llu w=%llu expect 1/1",
          (unsigned long long)mine->reads,
          (unsigned long long)mine->writes);

    /* The backing store must cover the WHOLE declared window, not a fixed
     * prefix. A 64-register array covered offsets 0x000-0x0FC while the two
     * registers actually measured on this SoC live at 0x320 (GPIO FSEL) and
     * 0x404 (CLOCK0 ADJ2) — so both were counted but not stored, and the stub
     * silently failed to be honest storage for exactly the registers that
     * mattered. */
    m.bus.write32(m.bus.ctx, 0x3f800320u, 0x0006070fu);
    CHECK(m.bus.read32(m.bus.ctx, 0x3f800320u) == 0x0006070fu,
          "off 0x320 read=%08x expect the value written (whole window backed)",
          m.bus.read32(m.bus.ctx, 0x3f800320u));
    CHECK(mine->oob == 0, "an in-window access must not count as oob");

    /* Past the declared window is a different matter: still counted, not
     * stored, so the shortfall stays visible instead of being pretended away. */
    (void)m.bus.read32(m.bus.ctx, 0x3f800000u + 0x1000u - 4u);
    CHECK(mine->oob == 0, "the final in-window word must still be backed");

    /* Overlap must be refused: silently shadowing a modelled device would be
     * far harder to notice than a rejected declaration. */
    CHECK(!s5l8900_add_stub(&m, 0x3b000800u, 0x1000u, "overlapping"),
          "an overlapping stub window must be refused");
    CHECK(!s5l8900_add_stub(&m, S5L8900_UART0_BASE, 0x1000u, "over-uart") ||
          m.bus.read32(m.bus.ctx, S5L8900_UART0_BASE + UART_UTRSTAT) & (1u << 2),
          "a stub must never take precedence over a real device model");

    s5l8900_free(&m);
}

static void test_mmio_width_alignment_and_window_edges(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    /* 0x3f100000, not 0x3b100000: the MBX aperture is the 16 MB the device tree
     * declares, so the old address is now inside a real device and a stub there
     * is correctly refused. This test is about stub LANE addressing, so it just
     * needs somewhere genuinely unclaimed. */
    CHECK(s5l8900_add_stub(&m, 0x3f100000u, 8u, "lanes"),
          "small stub declaration failed");

    /* Stub storage is byte-addressable and little-endian, including an
     * unaligned halfword that spans two backing uint32_t values. */
    m.bus.write32(m.bus.ctx, 0x3f100000u, 0x11223344u);
    m.bus.write8(m.bus.ctx, 0x3f100001u, 0xaau);
    CHECK(m.bus.read32(m.bus.ctx, 0x3f100000u) == 0x1122aa44u,
          "byte write updated the wrong register lane");
    m.bus.write16(m.bus.ctx, 0x3f100003u, 0xbeefu);
    CHECK(m.bus.read16(m.bus.ctx, 0x3f100003u) == 0xbeefu,
          "unaligned cross-register halfword did not round-trip");

    /* An access must fit wholly inside its window. Starting in the last byte
     * is not permission to spill into the next physical region. */
    uint64_t before = m.unmapped_reads;
    (void)m.bus.read16(m.bus.ctx, 0x3f100007u);
    CHECK(m.unmapped_reads == before + 1u,
          "cross-boundary stub read was treated as mapped");
    before = m.unmapped_reads;
    (void)m.bus.read16(m.bus.ctx, S5L8900_NOR_BASE + S5L8900_NOR_SIZE - 1u);
    CHECK(m.unmapped_reads == before + 1u,
          "cross-boundary NOR read was treated as mapped");

    /* Register devices are 32-bit MMIO. A byte store must not accidentally
     * become a full-register write, while UART TX deliberately accepts STRB. */
    uint64_t writes = m.unmapped_writes;
    m.bus.write8(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE, 1u);
    CHECK(m.unmapped_writes == writes + 1u && m.vic[0].enable == 0,
          "narrow VIC write clobbered a word register");
    m.bus.write8(m.bus.ctx, S5L8900_UART0_BASE + UART_UTXH, 'X');
    CHECK(m.uart0.tx_len == 1 && m.uart0.tx[0] == 'X',
          "aligned UART byte transmit should remain supported");
    s5l8900_free(&m);
}

static void test_address_space_wrap_is_refused(void) {
    s5l8900_t m;
    CHECK(!s5l8900_init(&m, 0, 0), "zero-sized RAM was accepted");
    CHECK(!s5l8900_init(&m, 0xffffff00u, 0x200u),
          "RAM aperture wrapping past 4 GiB was accepted");

    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    CHECK(!s5l8900_add_stub(&m, 0xf0000000u, 0x20000000u, "wrap"),
          "stub window wrapping past 4 GiB was accepted");
    s5l8900_free(&m);
}

/*
 * The power controller is the block that stood between a booting kernel and a
 * booting OS. AppleS5L8900XPowerController::start writes the domains it wants
 * gated and then spins until STATE agrees:
 *
 *     write(OFFCTRL, 0x12fc);  do { s = read(STATE); } while ((s & 0x12fc) != 0x12fc);
 *
 * Unmodelled, STATE read 0 forever: 3,887,707 reads, about a quarter of a
 * 200M-instruction boot, and start() never returned. The property under test is
 * the coupling — that ONCTRL and OFFCTRL are the only things that move STATE,
 * with the polarity the driver's own gate routine uses.
 */
static void test_power_gate_state_tracks_onctrl_offctrl(void) {
    s5l_power_t p; s5l_power_reset(&p);

    /* Gate: write-1-to-SET. This is the exact sequence and mask the driver
     * uses, so passing this is passing the loop that wedged the boot. */
    s5l_power_write(&p, POWER_OFFCTRL, 0x12fcu);
    uint32_t st = s5l_power_read(&p, POWER_STATE);
    CHECK((st & 0x12fcu) == 0x12fcu,
          "STATE=%08x expect bits 0x12fc set after OFFCTRL — this is the "
          "condition AppleS5L8900XPowerController::start spins on", st);

    /* Ungate: write-1-to-CLEAR. Bit 9 is USB, gated then ungated around
     * AppleS5L8900XUSBPhy::start. */
    s5l_power_write(&p, POWER_ONCTRL, 1u << 9);
    st = s5l_power_read(&p, POWER_STATE);
    CHECK((st & (1u << 9)) == 0, "STATE=%08x expect bit 9 cleared by ONCTRL", st);
    CHECK((st & (1u << 12)) != 0,
          "ONCTRL must clear only the bits written (bit 12 was gated too)");

    /* STATE is read-only: it moves via ONCTRL/OFFCTRL and nothing else. A
     * storage stub would have "worked" here by accident and failed in the
     * boot, because the guest never writes STATE at all. */
    uint32_t before = s5l_power_read(&p, POWER_STATE);
    s5l_power_write(&p, POWER_STATE, 0xffffffffu);
    CHECK(s5l_power_read(&p, POWER_STATE) == before,
          "STATE must be read-only");

    /* Writes outside the 14 modelled domains must not set phantom bits. */
    s5l_power_reset(&p);
    s5l_power_write(&p, POWER_ONCTRL, 0xffffffffu);
    s5l_power_write(&p, POWER_OFFCTRL, 0xffffffffu);
    CHECK(s5l_power_read(&p, POWER_STATE) == S5L_POWER_DOMAIN_MASK,
          "STATE=%08x expect only the 14 real domains",
          s5l_power_read(&p, POWER_STATE));
}

/* ------------------------------------------ Synopsys DWC2 USB OTG (config) ---
 *
 * The block behind 0x38400000 was unmodelled, so every read of it returned the
 * zero an unmapped access returns, and AppleSynopsysOTGDevice believed it: the
 * boot panicked in provideEndpointIDsForConfiguration ("ran of OUT endpoints",
 * AppleSynopsysOTGDevice.cpp:533) at retired instruction 8,728,148,009 — the
 * same instruction on every run.
 *
 * These three tests cover the three separable claims: the registers hold the
 * values we say they hold, PCGCCTL survives the read-modify-write the driver
 * wraps its reads in, and the endpoint derivation the driver runs on those
 * registers comes out self-consistent instead of panicking.
 */

/*
 * AppleSynopsysOTGDevice::findMaxEndpoints (kernel VA 0xc048c424), transcribed
 * from its disassembly. This is the driver's loop, not a paraphrase of it —
 * that is the whole point of running it here.
 *
 * One deliberate difference: the driver's loop does not bound `ep`, so a
 * unidirectional GHWCFG1 encoding walks it past 15 and off the end of its own
 * 15-byte endpoint lists. A test must not reproduce that by shifting by 32 or
 * more (undefined behaviour in C), so the shift is guarded and the iteration
 * count is capped and reported instead.
 */
typedef struct {
    unsigned in, out, max_endpoint, num_endpoints, iters;
    bool     aborted;      /* the driver's "11" direction encoding: give up */
} otg_endpoints_t;

static otg_endpoints_t otg_find_max_endpoints(uint32_t ghwcfg1,
                                              uint32_t ghwcfg2) {
    otg_endpoints_t r;
    memset(&r, 0, sizeof r);
    unsigned num_dev_eps = (ghwcfg2 >> 10) & 0xfu;
    unsigned ep = 1u;
    while (num_dev_eps >= r.in + r.out) {
        if (r.iters++ >= 64u) break;          /* the driver has no such guard */
        unsigned sh = 2u * ep;
        unsigned d = sh < 32u ? (ghwcfg1 >> sh) & 3u : 0u;
        if (d == 3u) { r.aborted = true; break; }
        if      (d == 0u) { r.in++; r.out++; }   /* bidirectional */
        else if (d == 1u) { r.in++;  }           /* IN only       */
        else              { r.out++; }           /* OUT only      */
        r.max_endpoint = ep;
        ep++;
    }
    r.num_endpoints = r.in + r.out + 2u;
    return r;
}

static void test_usb_otg_config_registers_read_their_specified_values(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t B = S5L8900_USB_OTG_BASE;

    CHECK(B == 0x38400000u,
          "USB OTG base 0x%08x expect 0x38400000 (/arm-io/usb-otg reg "
          "{0x400000,0x1000} over ranges child+0x38000000)", B);

    CHECK(m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG1) == S5L_DWC2_GHWCFG1,
          "GHWCFG1 = %08x expect %08x",
          m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG1), S5L_DWC2_GHWCFG1);
    CHECK(m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG2) == S5L_DWC2_GHWCFG2,
          "GHWCFG2 = %08x expect %08x",
          m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG2), S5L_DWC2_GHWCFG2);
    CHECK(m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG4) == S5L_DWC2_GHWCFG4,
          "GHWCFG4 = %08x expect %08x",
          m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG4), S5L_DWC2_GHWCFG4);
    CHECK(m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL) == 0u,
          "PCGCCTL must reset to 0, read %08x",
          m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL));

    /* The three GHWCFG words are hardware straps. A guest that writes them
     * must not be able to talk itself back into the endpoint count that
     * panicked. */
    m.bus.write32(m.bus.ctx, B + USBOTG_GHWCFG1, 0xffffffffu);
    m.bus.write32(m.bus.ctx, B + USBOTG_GHWCFG2, 0u);
    m.bus.write32(m.bus.ctx, B + USBOTG_GHWCFG4, 0xffffffffu);
    CHECK(m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG1) == S5L_DWC2_GHWCFG1 &&
          m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG2) == S5L_DWC2_GHWCFG2 &&
          m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG4) == S5L_DWC2_GHWCFG4,
          "GHWCFG1/2/4 must be read-only (got %08x %08x %08x)",
          m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG1),
          m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG2),
          m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG4));

    /*
     * Everything else in the page is unchanged from what an unmapped access
     * answered: reads return zero, writes are accepted and discarded. Modelling
     * four registers is not a licence to invent the other 1020, and GHWCFG3 at
     * 0x04c is checked by name because it is the one the driver does NOT read —
     * an invented value there would be a claim with nothing behind it.
     */
    static const uint32_t QUIET[] = { 0x000u, 0x04cu, 0x800u,
                                      S5L8900_DEV_SIZE - 4u };
    for (unsigned i = 0; i < sizeof QUIET / sizeof QUIET[0]; i++) {
        CHECK(m.bus.read32(m.bus.ctx, B + QUIET[i]) == 0u,
              "unmodelled +0x%03x must read 0, got %08x",
              QUIET[i], m.bus.read32(m.bus.ctx, B + QUIET[i]));
        m.bus.write32(m.bus.ctx, B + QUIET[i], 0xa5a5a5a5u);
        CHECK(m.bus.read32(m.bus.ctx, B + QUIET[i]) == 0u,
              "unmodelled +0x%03x stored a write; it must be discarded",
              QUIET[i]);
    }

    /* None of this traffic may be accounted as unmapped any more: that counter
     * is how a missing peripheral announces itself, and this one is present. */
    CHECK(m.unmapped_reads == 0 && m.unmapped_writes == 0,
          "USB OTG traffic still counted as unmapped (r=%llu w=%llu)",
          (unsigned long long)m.unmapped_reads,
          (unsigned long long)m.unmapped_writes);

    s5l8900_free(&m);
}

/*
 * The exact MMIO trace findMaxEndpoints performs, replayed against the machine
 * alone. Read from its disassembly, in order:
 *
 *   read PCGCCTL / write PCGCCTL &= ~3 / read GHWCFG2 / read GHWCFG1 /
 *   read PCGCCTL / write PCGCCTL |= 1
 *
 * so PCGCCTL has to be a word that remembers what it was given: the second read
 * must see the effect of the first write, or the driver's |= 1 lands on a value
 * it never wrote. Nothing here gates a clock — there is no clock to gate — and
 * nothing self-clears.
 */
static void test_usb_otg_pcgcctl_round_trips_the_drivers_sequence(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t B = S5L8900_USB_OTG_BASE;

    /* Plain storage first, across the whole width. */
    m.bus.write32(m.bus.ctx, B + USBOTG_PCGCCTL, 0xdeadbeefu);
    CHECK(m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL) == 0xdeadbeefu,
          "PCGCCTL did not round-trip: %08x",
          m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL));

    /* Seed a value with both gate bits set plus unrelated bits, so "&= ~3"
     * clearing exactly two bits is distinguishable from it clearing the word. */
    m.bus.write32(m.bus.ctx, B + USBOTG_PCGCCTL, 0x00000013u);

    uint32_t v = m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL);   /* 1. read  */
    m.bus.write32(m.bus.ctx, B + USBOTG_PCGCCTL, v & ~3u);      /* 2. ungate */
    CHECK(m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL) == 0x00000010u,
          "PCGCCTL &= ~3 = %08x expect 00000010 (bits 0,1 clear, bit 4 kept)",
          m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL));

    uint32_t cfg2 = m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG2); /* 3. */
    uint32_t cfg1 = m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG1); /* 4. */
    CHECK(cfg2 == S5L_DWC2_GHWCFG2 && cfg1 == S5L_DWC2_GHWCFG1,
          "the ungated reads must still see the straps (%08x %08x)",
          cfg1, cfg2);

    v = m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL);            /* 5. read  */
    CHECK(v == 0x00000010u,
          "the second PCGCCTL read must see the first write, got %08x", v);
    m.bus.write32(m.bus.ctx, B + USBOTG_PCGCCTL, v | 1u);       /* 6. gate  */
    CHECK(m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL) == 0x00000011u,
          "PCGCCTL |= 1 = %08x expect 00000011",
          m.bus.read32(m.bus.ctx, B + USBOTG_PCGCCTL));

    CHECK(m.unmapped_reads == 0 && m.unmapped_writes == 0,
          "the driver's own access trace must not touch anything unmapped");

    s5l8900_free(&m);
}

/*
 * THE HEADLINE: the derivation that panicked, run on the registers this model
 * now supplies.
 *
 * findMaxEndpoints computes the endpoint layout straight out of GHWCFG1 and
 * GHWCFG2, and provideEndpointIDsForConfiguration panics when that layout
 * cannot satisfy the configuration it is asked for. Reading the modelled
 * registers back through the bus and running the driver's own loop over them is
 * therefore the test that the panic is gone — not a claim about it.
 *
 * 5 IN / 5 OUT is enough for THIS guest, read from the guest rather than
 * assumed: its USB configuration plist in the rootfs (iPhone1,2,
 * standardMuxPTPEthernet) needs 2 OUT / 3 IN pipes by default and 3 OUT / 4 IN
 * at worst.
 */
static void test_usb_otg_endpoint_derivation_is_self_consistent(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t B = S5L8900_USB_OTG_BASE;

    uint32_t cfg1 = m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG1);
    uint32_t cfg2 = m.bus.read32(m.bus.ctx, B + USBOTG_GHWCFG2);
    otg_endpoints_t e = otg_find_max_endpoints(cfg1, cfg2);

    CHECK(!e.aborted, "the direction encoding must never hit the driver's "
                      "\"11\" abort case");
    CHECK(e.in == 5u && e.out == 5u && e.max_endpoint == 5u &&
          e.num_endpoints == 12u,
          "in=%u out=%u max_endpoint=%u num_endpoints=%u; expected 5/5/5/12",
          e.in, e.out, e.max_endpoint, e.num_endpoints);

    /* NumDevEps is the field the whole derivation turns on; pin it separately
     * so a wrong GHWCFG2 is reported as a wrong field, not just a wrong total. */
    CHECK(((cfg2 >> 10) & 0xfu) == 9u,
          "NumDevEps = %u expect 9", (unsigned)((cfg2 >> 10) & 0xfu));

    /* The driver's loop has no bound on `ep`, and its endpoint lists are 15
     * bytes. An all-bidirectional GHWCFG1 keeps it far short of that; any
     * unidirectional encoding would not. */
    CHECK(e.max_endpoint <= 15u,
          "max_endpoint=%u would run past the driver's 15-byte endpoint lists",
          e.max_endpoint);

    /* What the guest actually asks for, worst case, from its own plist. */
    CHECK(e.out >= 3u && e.in >= 4u,
          "worst-case standardMuxPTPEthernet needs 3 OUT / 4 IN; got %u/%u",
          e.out, e.in);

    /*
     * The contrast that proves the loop above is the driver's and not a
     * flattering rewrite: fed the zeros an unmodelled window returned, it
     * reproduces the panicking run's own printout exactly — "in EPs: 1 out,
     * EPs: 1, max_endpoint: 1, num_endpoints: 4".
     */
    otg_endpoints_t z = otg_find_max_endpoints(0u, 0u);
    CHECK(z.in == 1u && z.out == 1u && z.max_endpoint == 1u &&
          z.num_endpoints == 4u,
          "all-zero registers should reproduce the panic input 1/1/1/4, got "
          "%u/%u/%u/%u", z.in, z.out, z.max_endpoint, z.num_endpoints);

    printf("  [otg] GHWCFG1=%08x GHWCFG2=%08x -> in=%u out=%u max_ep=%u "
           "num_eps=%u (unmodelled was 1/1/1/4)\n",
           cfg1, cfg2, e.in, e.out, e.max_endpoint, e.num_endpoints);

    s5l8900_free(&m);
}

/*
 * The peripheral windows the machine declares for itself. The property under
 * test is not "a stub exists" but that each one is at the address two
 * independent sources agree on — the device tree's arm-io ranges and a walk of
 * the guest's live page tables — and that declaring them all actually
 * succeeded. A refused declaration leaves the window unmapped, which reads back
 * as zero and is exactly the silent guess stubs exist to replace.
 */
static void test_machine_declares_its_known_windows(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    CHECK(m.stub_declare_failures == 0,
          "%u stub declarations were refused at init", m.stub_declare_failures);

    static const struct { uint32_t addr; const char *name; } WANT[] = {
        { S5L8900_CLOCK_BASE,  "clkrstgen" },
        { S5L8900_MIU_BASE,    "miu"       },
        { S5L8900_GPIO_BASE,   "gpio"      },
        { S5L8900_EDGEIC_BASE, "edgeic"    },
        { S5L8900_GPIOIC_BASE, "gpioic"    },
    };
    for (unsigned i = 0; i < sizeof WANT / sizeof WANT[0]; i++) {
        /* Reads must come back as storage, not as the unmapped-access zero. */
        m.bus.write32(m.bus.ctx, WANT[i].addr + 8u, 0xc0ffee00u + i);
        CHECK(m.bus.read32(m.bus.ctx, WANT[i].addr + 8u) == 0xc0ffee00u + i,
              "%s at 0x%08x did not read back what was written",
              WANT[i].name, WANT[i].addr);
    }
    CHECK(m.unmapped_reads == 0 && m.unmapped_writes == 0,
          "declared windows must not count as unmapped (r=%llu w=%llu)",
          (unsigned long long)m.unmapped_reads,
          (unsigned long long)m.unmapped_writes);

    /* The two registers we have actually measured on this SoC live past the
     * first 256 bytes; check them specifically, because a short backing store
     * once swallowed exactly these. */
    m.bus.write32(m.bus.ctx, S5L8900_GPIO_BASE + 0x320u, 0x0006070fu);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_GPIO_BASE + 0x320u) == 0x0006070fu,
          "GPIO FSEL at +0x320 must be backed");
    m.bus.write32(m.bus.ctx, S5L8900_MIU_BASE + 0x404u, 0x12345678u);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_MIU_BASE + 0x404u) == 0x12345678u,
          "MIU +0x404 must be backed");

    /* The gpioic stub must not shadow the power controller: they share a page
     * and only 0x00-0x7f belongs to power. */
    m.bus.write32(m.bus.ctx, S5L8900_POWER_BASE + POWER_OFFCTRL, 0x12fcu);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_POWER_BASE + POWER_STATE) == 0x12fcu,
          "the gpioic stub must not take over the power controller's half");

    s5l8900_free(&m);
}

/*
 * SPI2 is the same controller model as SPI0 and SPI1. It additionally has the
 * M68 Z1 slave attached by the board, so controller register semantics are
 * tested here without assuming that the baseband transport is implemented.
 */
static void test_baseband_spi_window_reads_back_its_configuration(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    static const struct { uint32_t off, val; } REGS[] = {
        { SPI_CONTROL, 0x0000000du }, { SPI_SETUP, 0x0000000fu },
        { SPI_PIN, 0x00000000u }, { SPI_CLKDIV, 0x00000002u },
        { SPI_CNT, 0x00000000u }, { SPI_IDD, 0x00000000u },
    };
    for (unsigned i = 0; i < sizeof REGS / sizeof REGS[0]; i++) {
        m.bus.write32(m.bus.ctx, S5L8900_SPI2_BASE + REGS[i].off,
                      REGS[i].val);
        CHECK(m.bus.read32(m.bus.ctx, S5L8900_SPI2_BASE + REGS[i].off)
                  == REGS[i].val,
              "spi2 +0x%02x register readback mismatch",
              REGS[i].off);
    }

    /* None of this traffic may be accounted as unmapped any more. */
    CHECK(m.unmapped_reads == 0 && m.unmapped_writes == 0,
          "spi2 traffic still counted as unmapped (r=%llu w=%llu)",
          (unsigned long long)m.unmapped_reads,
          (unsigned long long)m.unmapped_writes);

    /* The three SPI windows are distinct and must not have been folded
     * together or onto a neighbour: spi2 sits two pages above the crypto block
     * the guest also touches. spi0 and spi1 are controllers rather than
     * storage now, so the check is that a store into either leaves spi2's
     * SETUP alone and lands in the right controller. */
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_SETUP, 0xa0a0a0a0u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_SETUP, 0xb1b1b1b1u);
    CHECK(m.spi[0].setup == 0xa0a0a0a0u && m.spi[1].setup == 0xb1b1b1b1u &&
          m.spi[2].setup == 0x0000000fu,
          "the three SPI windows must be independent");

    s5l8900_free(&m);
}

/* ------------------------------------------------- RAM vs device routing ---
 *
 * The bug these four tests exist for: at -R 512 the DRAM window
 * 0x08000000..0x28000000 reached over the NOR, bus_read tested RAM first and
 * returned before it ever consulted a device, so every NOR read came back as
 * whatever byte of the RAM disk happened to live there. No fault, no log, no
 * counter — and NOR is where an era-appropriate untether (24kpwn) has to
 * persist, so "we boot fine at -R 512" and "NOR works" could not both be true.
 */

/*
 * THE HEADLINE: at the geometry the real boot uses, a NOR read is NOR's bytes.
 *
 * "It read back what I programmed" is not on its own proof — RAM would do that
 * too. What proves the read reached the flash MODEL is a flash-only behaviour:
 * programming can clear bits but never set one back to 1. RAM would happily
 * take 0xffffffff; NOR must refuse it and keep the old value.
 */
static void test_nor_reads_are_nor_at_the_boot_ram_size(void) {
    /* Exactly what `bootkernel -R 512` builds: DRAM 0x08000000..0x28000000. */
    const uint32_t base = S5L8900_SDRAM_BASE, size = 512u << 20;
    s5l8900_t m;
    CHECK(s5l8900_init(&m, base, size),
          "the -R 512 geometry must still be constructible");
    if (!m.ram) return;

    /* Distinctive "RAM disk" bytes in the last word of DRAM, immediately below
     * the NOR window: if routing ever slips by one window again, this is the
     * value a NOR read would come back with. */
    const uint32_t ramdisk = 0xa5a5a5a5u;
    m.bus.write32(m.bus.ctx, base + size - 4u, ramdisk);
    CHECK(m.bus.read32(m.bus.ctx, base + size - 4u) == ramdisk,
          "the last word of DRAM must still be RAM");

    /* Known bytes in the flash, laid down as a factory flasher would. */
    const uint32_t flashed = 0x0f0f0f0fu;
    s5l_nor_program(&m.nor, 0x40u, &flashed, 4);

    uint32_t got = m.bus.read32(m.bus.ctx, S5L8900_NOR_BASE + 0x40u);
    CHECK(got == flashed,
          "NOR read at 0x%08x = %08x, expect %08x — it is being shadowed",
          S5L8900_NOR_BASE + 0x40u, got, flashed);
    CHECK(got != ramdisk, "the NOR read returned the RAM pattern");
    CHECK(m.unmapped_reads == 0,
          "the NOR window must be mapped, not counted unmapped (%llu)",
          (unsigned long long)m.unmapped_reads);

    /* The flash-only property. RAM cannot pass this. */
    m.bus.write32(m.bus.ctx, S5L8900_NOR_BASE + 0x40u, 0xffffffffu);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_NOR_BASE + 0x40u) == flashed,
          "a 1-setting write was accepted: this window is RAM, not flash");

    /* ...and clearing bits, the direction real programming goes, must work
     * through the bus — this is the path a persisted payload takes. */
    m.bus.write32(m.bus.ctx, S5L8900_NOR_BASE + 0x40u, 0x03030303u);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_NOR_BASE + 0x40u) == 0x03030303u,
          "programming through the bus did not take");

    printf("  [-R 512] DRAM %08x..%08x, NOR@%08x reads %08x (RAM held %08x)\n",
           base, base + size, S5L8900_NOR_BASE + 0x40u,
           m.bus.read32(m.bus.ctx, S5L8900_NOR_BASE + 0x40u), ramdisk);
    s5l8900_free(&m);
}

/*
 * The general guard, stated as the invariant rather than as one special case:
 * a machine that CONSTRUCTS never has RAM sitting on top of anything it
 * decodes. Probing every window's own edge is what makes this general — add a
 * device tomorrow and it is covered without touching this test.
 */
static void test_no_window_the_machine_decodes_is_shadowed_by_ram(void) {
    s5l8900_t base_m;
    CHECK(s5l8900_init(&base_m, 0, 1u << 20), "machine init failed");
    s5l_window_t w[S5L_WINDOW_MAX];
    unsigned nw = s5l8900_windows(&base_m, w, S5L_WINDOW_MAX);
    CHECK(nw > 0 && nw <= S5L_WINDOW_MAX,
          "window count %u is outside S5L_WINDOW_MAX", nw);
    s5l8900_free(&base_m);

    for (unsigned i = 0; i < nw && i < S5L_WINDOW_MAX; i++) {
        /* A 8 KiB aperture straddling this window's first byte. Deliberately
         * tiny: if the guard were broken the test must fail, not allocate a
         * gigabyte. */
        uint32_t rb = w[i].base - 0x1000u, rs = 0x2000u;
        s5l8900_t t;
        if (!s5l8900_init(&t, rb, rs)) {
            /* Refused. That is a correct outcome, and it must be explicable. */
            CHECK(s5l8900_ram_conflict(rb, rs) != NULL,
                  "init refused RAM 0x%08x+0x%x but names no conflict", rb, rs);
            continue;
        }
        /* Constructed. Then nothing it decodes may be under RAM — including
         * the window we aimed at, which must therefore have been dropped. */
        s5l_window_t got[S5L_WINDOW_MAX];
        unsigned ng = s5l8900_windows(&t, got, S5L_WINDOW_MAX);
        for (unsigned k = 0; k < ng && k < S5L_WINDOW_MAX; k++)
            CHECK(!s5l8900_overlaps(got[k].base, got[k].size, t.ram_base, t.ram_size),
                  "%s 0x%08x+0x%x is under RAM 0x%08x+0x%x",
                  got[k].name, got[k].base, got[k].size, t.ram_base, t.ram_size);
        /* A window that had to be dropped is never dropped silently. */
        CHECK(ng == nw || t.stub_declare_failures > 0,
              "a window vanished (%u -> %u) without being counted", nw, ng);
        s5l8900_free(&t);
    }

    /* Adjacency is not overlap: RAM ending exactly at a window's base is fine,
     * and is precisely the -R 512 case. */
    s5l8900_t ok;
    CHECK(s5l8900_init(&ok, S5L8900_NOR_BASE - 0x1000u, 0x1000u),
          "RAM ending exactly at the NOR base must be allowed");
    CHECK(s5l8900_ram_conflict(S5L8900_NOR_BASE - 0x1000u, 0x1000u) == NULL,
          "abutting RAM must not be reported as a conflict");
    s5l8900_free(&ok);

    /* And one byte more must not be. */
    CHECK(s5l8900_ram_conflict(S5L8900_NOR_BASE - 0x1000u, 0x1001u) != NULL,
          "RAM overlapping the NOR by one byte must be a conflict");
    s5l8900_t bad;
    CHECK(!s5l8900_init(&bad, S5L8900_SDRAM_BASE, 0x20100000u),
          "a DRAM window that reaches into the NOR must be refused, not aliased");
}

/*
 * Where the NOR window is allowed to be. Both bounds are evidence, not taste:
 *
 *   - it must sit above the largest DRAM the guest kernel can use. xnu-1357's
 *     arm_vm_init fixes virtual_avail at 0xe0000000, so with gVirtBase
 *     0xc0000000 mem_size tops out at 512 MB and DRAM cannot pass
 *     0x08000000 + 512 MB = 0x28000000;
 *   - it must sit below arm-io at 0x38000000, which /arm-io's ranges claim
 *     wholesale, and outside 0x18000000..0x28000000, the SoC's other claimed
 *     window (edram, vrom, sram/amc live in it).
 *
 * 0x24000000 satisfied neither: it was inside the DRAM we boot with, and inside
 * a range the device tree assigns to the SoC.
 */
static void test_the_nor_window_is_out_of_every_drams_reach(void) {
    const uint32_t max_dram_end = S5L8900_SDRAM_BASE + (512u << 20);
    CHECK(S5L8900_NOR_BASE >= max_dram_end,
          "NOR base 0x%08x is inside the largest usable DRAM (ends 0x%08x)",
          S5L8900_NOR_BASE, max_dram_end);
    CHECK((uint64_t)S5L8900_NOR_BASE + S5L8900_NOR_SIZE <= S5L8900_ARMIO_BASE,
          "the NOR window runs into arm-io at 0x%08x", S5L8900_ARMIO_BASE);
    CHECK(s5l8900_ram_conflict(S5L8900_SDRAM_BASE, 512u << 20) == NULL,
          "the -R 512 DRAM window must conflict with nothing");
    /* The value it used to have, pinned as a regression rather than a memory. */
    CHECK(s5l8900_ram_conflict(S5L8900_SDRAM_BASE, 512u << 20) == NULL &&
          0x24000000u < max_dram_end,
          "0x24000000 was inside the -R 512 DRAM window; that is why it moved");
}

/*
 * The SoC's own regions, as the shipped device tree gives them. This is the map
 * the routing contract is argued from, so it is worth pinning: /arm-io ranges
 * are (child, parent, size) {0,0x38000000,0x08000000} and
 * {0x10000000,0x18000000,0x10000000}, and the second is anchored twice — vrom's
 * reg 0x18000000 lands on 0x20000000 (the known S5L8900 boot-ROM address) and
 * amc's second reg 0x1a000000 lands on 0x22000000, which is also the link
 * address carried inside firmware/llb.bin.
 */
static void test_soc_regions_match_the_device_tree(void) {
    const s5l_window_t *r = NULL;
    unsigned n = s5l8900_soc_regions(&r);
    CHECK(n > 0 && r != NULL, "the SoC region table must not be empty");

    CHECK(S5L8900_ARMIO_BASE == 0x38000000u && S5L8900_ARMIO_SIZE == 0x08000000u,
          "arm-io = ranges triple 1's parent+size");
    CHECK(S5L8900_VROM_BASE == 0x20000000u,
          "vrom reg 0x18000000 over ranges triple 2 (+0x08000000) = 0x20000000");
    CHECK(S5L8900_SRAM_BASE == 0x22000000u && S5L8900_SRAM_SIZE == 0x0002c000u,
          "amc reg[1] {0x1a000000,0x2c000} over the same range = 0x22000000");
    CHECK(S5L8900_EDRAM_BASE == 0x18000000u,
          "edram reg 0x10000000 over the same range = 0x18000000");
    CHECK(S5L8900_SDRAM_BASE == 0x08000000u, "DRAM base");

    /* No region may overlap another, or the derivation is wrong somewhere. */
    for (unsigned i = 0; i < n; i++)
        for (unsigned k = i + 1; k < n; k++)
            CHECK(!s5l8900_overlaps(r[i].base, r[i].size, r[k].base, r[k].size),
                  "SoC regions %s and %s overlap", r[i].name, r[k].name);

    /*
     * Said out loud, because it is the honest limit of what we claim: the SoC
     * decodes edram at 0x18000000, so no DRAM above 256 MB can be physically
     * real on this part. We allow bigger anyway — a RAM-disk root does not fit
     * otherwise — and an oversized window therefore covers regions the hardware
     * has. That is not a silent alias: we do not model those regions, so we
     * decode nothing there, and the moment one of them becomes a device model
     * it joins DEVICE_WINDOWS and an oversized -R stops constructing.
     */
    unsigned covered = 0;
    for (unsigned i = 0; i < n; i++)
        if (r[i].base > S5L8900_SDRAM_BASE &&
            s5l8900_overlaps(S5L8900_SDRAM_BASE, 512u << 20, r[i].base, r[i].size))
            covered++;
    CHECK(covered == 3,
          "expected the -R 512 window to cover edram, vrom and sram/amc, got %u",
          covered);
    printf("  [map] DRAM 0x%08x, edram 0x%08x, vrom 0x%08x, sram 0x%08x, "
           "arm-io 0x%08x, NOR (ours) 0x%08x\n",
           S5L8900_SDRAM_BASE, S5L8900_EDRAM_BASE, S5L8900_VROM_BASE,
           S5L8900_SRAM_BASE, S5L8900_ARMIO_BASE, S5L8900_NOR_BASE);
}

/*
 * S5L8900_GPIO_BASE was 0x3cf00000 — the S5L8720-era address. The device tree
 * says /arm-io/gpio is reg {0x6400000,0x1000} over ranges child+0x38000000,
 * and the guest's page tables agree. It was unused, so it was a landmine
 * rather than a live bug; this pins it so it cannot drift back.
 */
static void test_gpio_base_is_the_s5l8900_address(void) {
    CHECK(S5L8900_GPIO_BASE == 0x3e400000u,
          "S5L8900_GPIO_BASE=0x%08x expect 0x3e400000 (device tree + page walk)",
          S5L8900_GPIO_BASE);
}

/*
 * There are two PL192 VICs (device tree: reg {0xe00000,0x2000}, vic-stride
 * 0x1000) and AppleARMPL192VIC maps both pages. VIC1 used to be unmapped, so
 * its registers read back as zero and its outputs went nowhere — meaning any
 * device on a line above 31 (the watchdog at 0x33, SDIO at 0x2a, GPIO at
 * 0x20/0x21) could be correctly programmed and still never reach the CPU.
 */
static void test_vic1_is_mapped_and_drives_the_cpu(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    /* It is a real controller, not a hole: enables read back. */
    m.bus.write32(m.bus.ctx, S5L8900_VIC1_BASE + VIC_INTENABLE, 1u << 3);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_VIC1_BASE + VIC_INTENABLE) == (1u << 3),
          "VIC1 INTENABLE=%08x expect bit 3",
          m.bus.read32(m.bus.ctx, S5L8900_VIC1_BASE + VIC_INTENABLE));
    CHECK(m.unmapped_reads == 0 && m.unmapped_writes == 0,
          "VIC1 accesses must not be unmapped");

    /* And it reaches the CPU. Assert a line on VIC1 only and tick. */
    s5l_vic_set_line(&m.vic[1], 3, true);
    s5l8900_tick(&m, 1);
    CHECK(m.cpu.irq_line,
          "an enabled VIC1 line must raise the CPU's IRQ — otherwise every "
          "device-tree interrupt above 31 is unreachable");

    /* Routing still works independently on each controller. */
    m.bus.write32(m.bus.ctx, S5L8900_VIC1_BASE + VIC_INTSELECT, 1u << 3);
    s5l8900_tick(&m, 1);
    CHECK(!m.cpu.irq_line && m.cpu.fiq_line, "VIC1 select must route to FIQ");

    s5l8900_free(&m);
}

/*
 * AppleH1DisplayDrivers maps three separate pages, not one monolithic TV
 * device.  Unknown words are retained as byte-addressable storage, while the
 * first word's ready bit is hardware-owned.  Exercise both properties here:
 * an unaligned halfword crosses backing words, and mixer bit 2 survives the
 * run/ready handshake (the real start path writes mixer control = 5).
 */
static void test_tvout_register_lanes_and_idle_handshake(void) {
    s5l_tvout_t t;
    s5l_tvout_reset(&t, S5L8900_TB_HZ);

    for (unsigned bank = 0; bank < S5L_TVOUT_BANK_COUNT; bank++)
        CHECK(s5l_tvout_read(&t, (s5l_tvout_bank_t)bank, 0, 4) ==
              TVOUT_READY,
              "TV-out bank %u did not reset idle/ready", bank);
    CHECK((s5l_tvout_read(&t, S5L_TVOUT_BANK_SDO,
                           TVOUT_SDO_IRQMASK, 4) &
           TVOUT_SDO_MASK_VSYNC) != 0u,
          "SDO VSYNC must reset masked");
    CHECK(t.frame_ticks == S5L8900_TB_HZ / S5L_TVOUT_REFRESH_HZ,
          "TV-out period=%u expect %u", t.frame_ticks,
          S5L8900_TB_HZ / S5L_TVOUT_REFRESH_HZ);

    s5l_tvout_write(&t, S5L_TVOUT_BANK_CTRL, 0x100u,
                     0x11223344u, 4);
    s5l_tvout_write(&t, S5L_TVOUT_BANK_CTRL, 0x103u, 0xbeefu, 2);
    CHECK(s5l_tvout_read(&t, S5L_TVOUT_BANK_CTRL, 0x100u, 4) ==
          0xef223344u,
          "unaligned TV-out write updated the wrong byte lanes");
    CHECK(s5l_tvout_read(&t, S5L_TVOUT_BANK_CTRL, 0x103u, 2) ==
          0xbeefu,
          "cross-word TV-out halfword did not round-trip");
    CHECK(s5l_tvout_read(&t, S5L_TVOUT_BANK_CTRL,
                         S5L_TVOUT_BANK_SIZE - 1u, 2) == 0u,
          "cross-bank direct read was accepted");

    s5l_tvout_write(&t, S5L_TVOUT_BANK_MIXER, 0, 5u, 4);
    CHECK(s5l_tvout_read(&t, S5L_TVOUT_BANK_MIXER, 0, 4) == 5u,
          "mixer start lost bit 2 or exposed ready early");
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO, 0, TVOUT_RUN, 2);
    CHECK(s5l_tvout_running(&t) &&
          s5l_tvout_read(&t, S5L_TVOUT_BANK_CTRL, 0, 4) == TVOUT_READY,
          "run19's control-ready/mixer-5/SDO-1 state did not run TV timing");

    s5l_tvout_write(&t, S5L_TVOUT_BANK_MIXER, 0, 4u, 4);
    CHECK(!s5l_tvout_running(&t) &&
          s5l_tvout_read(&t, S5L_TVOUT_BANK_MIXER, 0, 4) == 6u,
          "mixer stop must derive ready bit 1 and preserve bit 2");

    /* IRQ38's mixer source is deliberately nonasserting.  Guest ACK writes
     * are W1C and must not read back as a made-up hotplug event. */
    s5l_tvout_write(&t, S5L_TVOUT_BANK_MIXER,
                     TVOUT_MIXER_STATUS, 3u, 4);
    CHECK(s5l_tvout_read(&t, S5L_TVOUT_BANK_MIXER,
                         TVOUT_MIXER_STATUS, 4) == 0u,
          "mixer W1C ACK fabricated status");
}

/*
 * Run19 captured the shipped resume state exactly as CTRL/MIXER/SDO = 0/5/1,
 * with SDO +0x284 = 0.  The resume routine never sets CTRL+0, and the IRQ30
 * filter never reads it, so treating that independent coefficient-block
 * handshake as a scan-timing gate suppresses every frame.  Its transitions
 * must also leave the already-running mixer+SDO phase untouched.
 */
static void test_tvout_run19_resume_state_and_control_phase(void) {
    s5l_tvout_t t;
    s5l_tvout_reset(&t, 600u);
    s5l_tvout_write(&t, S5L_TVOUT_BANK_MIXER, 0, 5u, 4);
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO, 0, TVOUT_RUN, 4);
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO,
                     TVOUT_SDO_IRQMASK, 0u, 4);

    CHECK(s5l_tvout_running(&t) &&
          s5l_tvout_read(&t, S5L_TVOUT_BANK_CTRL, 0, 4) == TVOUT_READY,
          "run19 0/5/1 state was not live with control independently ready");
    CHECK(!s5l_tvout_tick(&t, 4u) && t.frame_accum == 4u,
          "run19 state did not begin accumulating a frame");

    s5l_tvout_write(&t, S5L_TVOUT_BANK_CTRL, 0, TVOUT_RUN, 4);
    CHECK(s5l_tvout_running(&t) && t.frame_accum == 4u,
          "starting the independent control block reset TV timing");
    s5l_tvout_write(&t, S5L_TVOUT_BANK_CTRL, 0, 0u, 4);
    CHECK(s5l_tvout_running(&t) && t.frame_accum == 4u &&
          s5l_tvout_read(&t, S5L_TVOUT_BANK_CTRL, 0, 4) == TVOUT_READY,
          "stopping the independent control block reset or gated TV timing");

    CHECK(s5l_tvout_tick(&t, 6u) && t.frames == 1u &&
          t.frame_accum == 0u,
          "run19 state did not deliver VSYNC after one complete frame");
}

/*
 * SDO +0x280 is the swap-completion latch and +0x284 masks it.  The IRQ action
 * in AppleH1DisplayDrivers writes bit 0 back to +0x280 before dequeuing the
 * pending swap, so W1C behavior and level deassertion are boot-critical.
 */
static void test_tvout_vsync_w1c_mask_and_phase(void) {
    s5l_tvout_t t;
    s5l_tvout_reset(&t, 600u);
    CHECK(t.frame_ticks == 10u, "600 Hz timebase should give a 10-tick frame");

    s5l_tvout_write(&t, S5L_TVOUT_BANK_CTRL, 0, TVOUT_RUN, 4);
    s5l_tvout_write(&t, S5L_TVOUT_BANK_MIXER, 0, 5u, 4);
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO, 0, TVOUT_RUN, 4);
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO,
                     TVOUT_SDO_IRQMASK, 0u, 4);

    CHECK(!s5l_tvout_tick(&t, 9u) && t.frames == 0u,
          "TV-out raised VSYNC before one complete frame");
    CHECK(s5l_tvout_tick(&t, 1u) && t.frames == 1u &&
          (s5l_tvout_read(&t, S5L_TVOUT_BANK_SDO,
                           TVOUT_SDO_IRQ, 4) & TVOUT_SDO_VSYNC) != 0u,
          "one full frame did not latch VSYNC");

    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO,
                     TVOUT_SDO_IRQMASK, TVOUT_SDO_MASK_VSYNC, 1);
    CHECK(!s5l_tvout_irq(&t) &&
          (s5l_tvout_read(&t, S5L_TVOUT_BANK_SDO,
                           TVOUT_SDO_IRQ, 4) & TVOUT_SDO_VSYNC) != 0u,
          "masking must gate the line without destroying pending status");
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO,
                     TVOUT_SDO_IRQMASK, 0u, 1);
    CHECK(s5l_tvout_irq(&t), "unmasking a pending VSYNC did not assert");
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO,
                     TVOUT_SDO_IRQ, TVOUT_SDO_VSYNC, 1);
    CHECK(!s5l_tvout_irq(&t) &&
          s5l_tvout_read(&t, S5L_TVOUT_BANK_SDO,
                          TVOUT_SDO_IRQ, 4) == 0u,
          "byte W1C did not clear/deassert VSYNC");

    CHECK(s5l_tvout_tick(&t, 25u) && t.frames == 3u &&
          t.frame_accum == 5u,
          "large TV-out tick lost boundaries or residual phase");
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO, 0, 0u, 1);
    CHECK(!s5l_tvout_irq(&t) && t.frame_accum == 0u,
          "stopping TV-out did not gate IRQ/reset phase");
    CHECK((s5l_tvout_read(&t, S5L_TVOUT_BANK_SDO,
                           TVOUT_SDO_IRQ, 4) & TVOUT_SDO_VSYNC) != 0u,
          "a run-gate transition silently ate latched VSYNC");
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO,
                     TVOUT_SDO_IRQ, TVOUT_SDO_VSYNC, 1);
    s5l_tvout_write(&t, S5L_TVOUT_BANK_SDO, 0, TVOUT_RUN, 1);
    CHECK(!s5l_tvout_tick(&t, 0u) && t.frame_accum == 0u,
          "restart manufactured an immediate stale VBlank");
}

/*
 * End-to-end: all three physical pages route through the machine, a generated
 * VSYNC reaches VIC0 line 30, and the guest's narrow W1C store drops it.
 */
static void test_tvout_machine_routing_and_irq30(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    s5l_window_t windows[S5L_WINDOW_MAX];
    unsigned nw = s5l8900_windows(&m, windows, S5L_WINDOW_MAX);
    /* 22 fixed device windows: nor, clcd, the three tv-out banks, i2c0, i2c1,
     * i2s0, i2s1, spi0, spi1, usb-otg, vic0, vic1, power, gpioic, gpio, uart0,
     * uart4, timer, dmac0, dmac1. This count is a tripwire for a window
     * silently vanishing from the table, so it moves only when a real device
     * model is added or removed — it went from 13 to 15 when spi0 and spi1
     * stopped being stubs, from 15 to 17 when the two halves of /arm-io/gpio
     * did, from 17 to 18 when uart4 became the guest's PPP line, from 18 to 20
     * when the two I2S controllers landed with the WM8991 codec, and from 20 to
     * 22 with the two PL080 DMA controllers that feed those I2S FIFOs,
     * and from 22 to 23 when the PowerVR MBX block was modelled so its
     * kext could get past its reset handshake. */
    CHECK(nw == m.stub_count + 24u,
          "fixed device-window count=%u expect 24 (+%u stubs)",
          nw - m.stub_count, m.stub_count);
    bool have_ctrl = false, have_mixer = false, have_sdo = false;
    for (unsigned i = 0; i < nw && i < S5L_WINDOW_MAX; i++) {
        if (windows[i].base == S5L8900_TVOUT_CTRL_BASE)
            have_ctrl = windows[i].size == S5L_TVOUT_BANK_SIZE &&
                        strcmp(windows[i].name, "tvout-control") == 0;
        if (windows[i].base == S5L8900_TVOUT_MIXER_BASE)
            have_mixer = windows[i].size == S5L_TVOUT_BANK_SIZE &&
                         strcmp(windows[i].name, "tvout-mixer") == 0;
        if (windows[i].base == S5L8900_TVOUT_SDO_BASE)
            have_sdo = windows[i].size == S5L_TVOUT_BANK_SIZE &&
                       strcmp(windows[i].name, "tvout-sdo") == 0;
    }
    CHECK(have_ctrl && have_mixer && have_sdo,
          "machine window table is missing or mis-sizing a TV-out bank");

    /* Byte-safe bus routing, including an unaligned write crossing words. */
    m.bus.write32(m.bus.ctx, S5L8900_TVOUT_CTRL_BASE + 0x100u,
                  0x11223344u);
    m.bus.write16(m.bus.ctx, S5L8900_TVOUT_CTRL_BASE + 0x103u, 0xbeefu);
    CHECK(m.bus.read16(m.bus.ctx,
                       S5L8900_TVOUT_CTRL_BASE + 0x103u) == 0xbeefu,
          "TV-out narrow bus routing lost byte lanes");
    uint64_t unmapped = m.unmapped_reads;
    (void)m.bus.read16(m.bus.ctx,
                       S5L8900_TVOUT_CTRL_BASE +
                       S5L_TVOUT_BANK_SIZE - 1u);
    CHECK(m.unmapped_reads == unmapped + 1u,
          "TV-out access crossing its 4 KiB page was mapped");

    m.cpu_hz = m.tb_hz = 1u;
    m.tvout.frame_ticks = 4u;
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE,
                  1u << S5L8900_IRQ_TVOUT);
    /* Exact run19 resume state: the independent control block is stopped and
     * ready while mixer+SDO remain the live scan-timing engines. */
    m.bus.write32(m.bus.ctx, S5L8900_TVOUT_MIXER_BASE, 5u);
    m.bus.write16(m.bus.ctx, S5L8900_TVOUT_SDO_BASE, TVOUT_RUN);
    m.bus.write8(m.bus.ctx,
                  S5L8900_TVOUT_SDO_BASE + TVOUT_SDO_IRQMASK, 0u);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_TVOUT_CTRL_BASE) ==
              TVOUT_READY,
          "run19 control block was not stopped/ready");

    s5l8900_tick(&m, 4u);
    CHECK(m.cpu.irq_line &&
          (m.bus.read32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_RAWINTR) &
           (1u << S5L8900_IRQ_TVOUT)) != 0u,
          "TV-out VSYNC did not reach VIC0 line 30");
    m.bus.write8(m.bus.ctx,
                  S5L8900_TVOUT_SDO_BASE + TVOUT_SDO_IRQ,
                  TVOUT_SDO_VSYNC);
    s5l8900_tick(&m, 0u);
    CHECK(!m.cpu.irq_line,
          "guest byte W1C did not deassert machine IRQ30");

    /* A mixer ACK must never create IRQ38 (VIC1 local line 6). */
    m.bus.write32(m.bus.ctx,
                  S5L8900_TVOUT_MIXER_BASE + TVOUT_MIXER_STATUS, 3u);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[1].raw & (1u << (38u - 32u))) == 0u,
          "mixer ACK fabricated IRQ38");

    s5l8900_free(&m);
}

static void test_tvout_wfi_fast_forwards_to_vsync(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");
    const uint32_t wfi = 0xee070f90u;
    s5l8900_load(&m, 0, &wfi, sizeof wfi);

    m.cpu_hz = m.tb_hz = 1u;
    m.tvout.frame_ticks = 7u;
    m.tvout.frame_accum = 5u;
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE,
                  1u << S5L8900_IRQ_TVOUT);
    m.bus.write32(m.bus.ctx, S5L8900_TVOUT_CTRL_BASE, TVOUT_RUN);
    m.bus.write32(m.bus.ctx, S5L8900_TVOUT_MIXER_BASE, 5u);
    m.bus.write32(m.bus.ctx, S5L8900_TVOUT_SDO_BASE, TVOUT_RUN);
    /* Final run transition deliberately resets phase; position it two ticks
     * before VSYNC only after all gates are live. */
    m.tvout.frame_accum = 5u;
    m.bus.write32(m.bus.ctx,
                  S5L8900_TVOUT_SDO_BASE + TVOUT_SDO_IRQMASK, 0u);
    m.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I;

    arm_status_t st = arm_step(&m.cpu);
    CHECK(st == ARM_OK && m.cpu.r[15] == 4u && m.cpu.cycles == 1u,
          "TV-out WFI status=%d pc=%08x cycles=%llu",
          (int)st, m.cpu.r[15], (unsigned long long)m.cpu.cycles);
    CHECK(m.tvout.frames == 1u && m.tvout.frame_accum == 0u &&
          m.cpu.irq_line,
          "WFI did not stop exactly at TV-out VSYNC: frames=%llu phase=%u irq=%d",
          (unsigned long long)m.tvout.frames, m.tvout.frame_accum,
          (int)m.cpu.irq_line);

    s5l8900_free(&m);
}

/*
 * The CLCD's reason to be a device model rather than a stub.
 *
 * AppleH1CLCD submits a framebuffer swap, sets bit 0 of the interrupt mask at
 * 0x014, and then waits for bit 0 of the status at 0x018. Storage that returns
 * what was last written returns zero forever and the swap never completes — the
 * UI wedges with no error at all. So the frame interrupt has to arrive on its
 * own, at about the panel's refresh rate in GUEST time.
 */
static void test_clcd_raises_the_frame_interrupt(void) {
    s5l_clcd_t c; s5l_clcd_reset(&c);

    /* Nothing before scanout starts, however long we wait. */
    s5l_clcd_write(&c, CLCD_INTMASK, CLCD_INT_FRAME);
    CHECK(!s5l_clcd_tick(&c, c.frame_ticks * 10u),
          "no frame interrupt before CLCD_ENABLE is written");
    CHECK(c.frames == 0, "frames=%llu expect 0 while stopped",
          (unsigned long long)c.frames);

    /* The driver's own order: program global/clock gates, mask first, enable
     * last. A start command without the other gates is remembered state, not
     * live scanout. */
    s5l_clcd_write(&c, CLCD_CTRL, CLCD_CTRL_ENABLE);
    s5l_clcd_write(&c, CLCD_GATE, 1u);
    s5l_clcd_write(&c, CLCD_ENABLE, 1);
    CHECK(!s5l_clcd_tick(&c, c.frame_ticks - 1u),
          "the first VBL must be a whole frame after start, not immediate");
    CHECK(s5l_clcd_tick(&c, 1), "a frame's worth of ticks must raise the line");
    CHECK((s5l_clcd_read(&c, CLCD_INTSTATUS) & CLCD_INT_FRAME) != 0,
          "status=%08x expect bit 0 set at VBL",
          s5l_clcd_read(&c, CLCD_INTSTATUS));

    /* Never the underrun bits: they only make a correct driver log errors. */
    CHECK((s5l_clcd_read(&c, CLCD_INTSTATUS) & CLCD_INT_UNDERRUN) == 0,
          "the underrun bits must never be asserted");

    /* Write-1-to-clear, exactly as the handler acknowledges at 0xc0705dac. */
    s5l_clcd_write(&c, CLCD_INTSTATUS, CLCD_INT_FRAME);
    CHECK(s5l_clcd_read(&c, CLCD_INTSTATUS) == 0,
          "status=%08x expect cleared by the driver's write-1 acknowledge",
          s5l_clcd_read(&c, CLCD_INTSTATUS));
    CHECK(!s5l_clcd_tick(&c, 0), "the line must drop once acknowledged");

    /* Steady, one per frame — not a burst and not a stall. */
    unsigned n = 0;
    for (unsigned i = 0; i < 10u * c.frame_ticks; i++)
        if (s5l_clcd_tick(&c, 1)) { n++; s5l_clcd_write(&c, CLCD_INTSTATUS, CLCD_INT_FRAME); }
    CHECK(n == 10, "raised %u frames in 10 frames' worth of ticks", n);
}

/* A host scheduler can hand the machine a very large time slice. The phase
 * arithmetic must not wrap at 32 bits and silently postpone VBL forever. */
static void test_clcd_large_tick_preserves_phase(void) {
    s5l_clcd_t c; s5l_clcd_reset(&c);
    c.frame_ticks = 100u;
    c.scanning = true;
    c.ctrl = CLCD_CTRL_ENABLE;
    c.gate = 1u;
    c.frame_accum = 99u;
    c.intmask = CLCD_INT_FRAME;

    const uint64_t total = UINT64_C(99) + UINT32_MAX;
    CHECK(s5l_clcd_tick(&c, UINT32_MAX),
          "a huge tick crossing a frame must raise the frame line");
    CHECK(c.frame_accum == (uint32_t)(total % c.frame_ticks),
          "phase=%u expect %u after a UINT32_MAX tick",
          c.frame_accum, (uint32_t)(total % c.frame_ticks));
    CHECK(c.frames == total / c.frame_ticks,
          "frames=%llu expect %llu after a UINT32_MAX tick",
          (unsigned long long)c.frames,
          (unsigned long long)(total / c.frame_ticks));
}

/*
 * The interrupt LINE must be gated by the hardware mask at 0x014, not by the
 * status alone. The handler at 0xc0705d7c computes `status & shadowMask` and
 * only acknowledges what is in that intersection. If the line were asserted for
 * a source the mask has turned off, the handler would clear nothing, return,
 * and be re-entered immediately — an interrupt storm, which is the same failure
 * that the timer's acknowledge mask exists to avoid.
 */
static void test_clcd_line_is_gated_by_the_mask(void) {
    s5l_clcd_t c; s5l_clcd_reset(&c);
    s5l_clcd_write(&c, CLCD_CTRL, CLCD_CTRL_ENABLE);
    s5l_clcd_write(&c, CLCD_GATE, 1u);
    s5l_clcd_write(&c, CLCD_ENABLE, 1);

    /* Mask clear: a frame still latches in the status (the hardware does not
     * stop counting) but nothing reaches the controller. */
    CHECK(!s5l_clcd_tick(&c, c.frame_ticks), "masked-off frame must not raise the line");
    CHECK((s5l_clcd_read(&c, CLCD_INTSTATUS) & CLCD_INT_FRAME) != 0,
          "the status latch should still record the frame");

    /* This is the driver's actual swap_submit sequence at 0xc0705d44: clear the
     * stale status FIRST, then enable. If the status were not clearable the
     * very first swap would look already-complete. */
    s5l_clcd_write(&c, CLCD_INTSTATUS, CLCD_INT_FRAME);
    s5l_clcd_write(&c, CLCD_INTMASK, 0x3f00u | CLCD_INT_FRAME);
    CHECK(!s5l_clcd_tick(&c, 0),
          "arming the mask must not immediately re-raise a cleared status");
    CHECK(s5l_clcd_tick(&c, c.frame_ticks), "the next frame must raise the line");

    /* And the underrun bits are enabled in that same mask (0x3f01), so if we
     * ever set them the driver would log an error every frame. */
    CHECK((s5l_clcd_read(&c, CLCD_INTSTATUS) & CLCD_INT_UNDERRUN) == 0,
          "underrun must stay clear even while it is unmasked");
}

/*
 * 0x204 must not read as 0xC0. The driver does
 *   0xc0705ccc  ldr r3,[reg,#0x204]; lsr r3,r3,#6; and r3,r3,#3; cmp r3,#3
 * and DEFERS the swap when both bits are set. A stub that echoed a previous
 * write, or any model that guessed at those bits, could stall every swap the
 * display ever attempts. The N82 polarity bit is stable state; the two live
 * status bits must remain "not busy" because this model has no scanline phase.
 */
static void test_clcd_status_never_defers_the_swap(void) {
    s5l_clcd_t c; s5l_clcd_reset(&c);
    CHECK(((s5l_clcd_read(&c, CLCD_STATUS) >> 6) & 3u) != 3u,
          "status=%08x has bits[7:6] set — AppleH1CLCD would defer every swap",
          s5l_clcd_read(&c, CLCD_STATUS));
    /* Not even if the guest writes it: it is read-only. */
    s5l_clcd_write(&c, CLCD_STATUS, 0xffffffffu);
    CHECK(s5l_clcd_read(&c, CLCD_STATUS) == 0,
          "status=%08x expect 0 — a write must not be able to stall the display",
          s5l_clcd_read(&c, CLCD_STATUS));

    c.gate = CLCD_N82_VIDCON0;
    CHECK(s5l_clcd_read(&c, CLCD_STATUS) == CLCD_N82_VIDCON1,
          "N82 inverted-VCLK polarity was not preserved: %08x",
          s5l_clcd_read(&c, CLCD_STATUS));
    s5l_clcd_write(&c, CLCD_STATUS, 0xffffffffu);
    CHECK(s5l_clcd_read(&c, CLCD_STATUS) == CLCD_N82_VIDCON1,
          "read-only N82 VIDCON1 was changed by a write: %08x",
          s5l_clcd_read(&c, CLCD_STATUS));
}

/*
 * Everything else on the page is storage the driver saves and restores across
 * sleep. 0x0d8..0x0ec are per-window configuration pairs (not panel timing);
 * the actual VIDTCON timing registers are 0x20c..0x218. Read-back is the
 * contract for both groups.
 */
/*
 * The per-frame event, and only that event.
 *
 * `updates` exists so instructions-per-frame can be measured -- nothing in
 * this project has ever measured it, and every projected frame rate rests on
 * it. A counter that over-counts would make frames look cheap and a dynarec
 * look unnecessary, so what does NOT increment it matters as much as what
 * does: CLCD_UPDATE is written with values other than 2, and the neighbouring
 * CLCD_UPDATE2 is a different register entirely.
 *
 * It is also distinct from `frames`, which counts VBL boundaries this model
 * manufactures. run76/83 measured 289 VBLANKs against 150 swaps.
 */
static void test_clcd_counts_only_real_window_updates(void) {
    s5l_clcd_t c; s5l_clcd_reset(&c);
    CHECK(c.updates == 0u, "a reset CLCD claims %llu updates",
          (unsigned long long)c.updates);

    s5l_clcd_write(&c, CLCD_UPDATE, 2u);
    s5l_clcd_write(&c, CLCD_UPDATE, 2u);
    CHECK(c.updates == 2u, "two update heads counted as %llu",
          (unsigned long long)c.updates);

    /* Not every store to the register is the head of an update. */
    s5l_clcd_write(&c, CLCD_UPDATE, 0u);
    s5l_clcd_write(&c, CLCD_UPDATE, 1u);
    s5l_clcd_write(&c, CLCD_UPDATE, 3u);
    CHECK(c.updates == 2u,
          "a non-2 store to CLCD_UPDATE was counted as a frame (%llu)",
          (unsigned long long)c.updates);

    /* And CLCD_UPDATE2 is a different register, not a second head. */
    s5l_clcd_write(&c, CLCD_UPDATE2, 2u);
    CHECK(c.updates == 2u,
          "CLCD_UPDATE2 was counted as a window update (%llu)",
          (unsigned long long)c.updates);

    /* The register still reads back whatever was last stored. */
    CHECK(s5l_clcd_read(&c, CLCD_UPDATE) == 3u,
          "CLCD_UPDATE read back %08x, not the 3 last written",
          s5l_clcd_read(&c, CLCD_UPDATE));

    /* VBL boundaries are a different thing and must not move it. */
    const uint64_t before = c.updates;
    c.scanning = true; c.ctrl = CLCD_CTRL_ENABLE; c.gate = 1u;
    c.frame_ticks = 4u; c.frame_accum = 3u;
    (void)s5l_clcd_tick(&c, 8u);
    CHECK(c.updates == before,
          "a VBL boundary incremented the guest update count (%llu -> %llu)",
          (unsigned long long)before, (unsigned long long)c.updates);
}

static void test_clcd_saved_registers_read_back(void) {
    s5l_clcd_t c; s5l_clcd_reset(&c);
    static const uint32_t OFFS[] = {
        CLCD_WINCFG0, CLCD_WINCFG0 + 4u, CLCD_WINCFG0 + 8u, CLCD_WINCFG0 + 12u,
        CLCD_WINCFG2_AUX, CLCD_CTRL, CLCD_FIFO, CLCD_BACKDROP,
        CLCD_VIDEO_FIRST, CLCD_VIDEO_LAST, CLCD_CSC_FIRST, CLCD_CSC_LAST,
        CLCD_OPAQUE_FIRST, CLCD_OPAQUE_LAST, CLCD_GATE,
        CLCD_GAMMA0, CLCD_GAMMA0 + CLCD_GAMMA_SIZE - 4u,
        CLCD_GAMMA0 + CLCD_GAMMA_SIZE, CLCD_GAMMA0 + 3u * CLCD_GAMMA_SIZE - 4u,
    };
    for (unsigned i = 0; i < sizeof OFFS / sizeof OFFS[0]; i++) {
        s5l_clcd_write(&c, OFFS[i], 0xa5000000u + i);
        CHECK(s5l_clcd_read(&c, OFFS[i]) == 0xa5000000u + i,
              "offset 0x%03x read=%08x expect %08x", OFFS[i],
              s5l_clcd_read(&c, OFFS[i]), 0xa5000000u + i);
    }
    /* The three LUTs must be distinct, not aliases of one another. */
    CHECK(s5l_clcd_read(&c, CLCD_GAMMA0) !=
          s5l_clcd_read(&c, CLCD_GAMMA0 + CLCD_GAMMA_SIZE),
          "the gamma LUTs must not alias each other");
}

/*
 * Seeding stands in for iBoot: window 0 programmed over a running scanout, so
 * IOMobileFramebuffer::start adopts our framebuffer instead of inventing one.
 * The property that matters is that what iBoot wrote is what the guest reads —
 * through the real register path, at the real offsets.
 */
static void test_clcd_seed_is_visible_to_the_guest(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    const uint32_t FB = 0x0ff00000u, W = 320, H = 480, STRIDE = 320 * 4;
    CHECK(s5l_clcd_seed_window0(&m.clcd, FB, W, H, STRIDE,
                                CLCD_FMT_32BPP, CLCD_ORDER_BGRA),
          "the iPhone 3G's 320x480 framebuffer must be representable");

    uint32_t b = S5L8900_CLCD_BASE + CLCD_WIN_FIRST;
    CHECK(m.bus.read32(m.bus.ctx, b + CLCD_WIN_FBADDR) == FB,
          "window 0 base=%08x expect %08x",
          m.bus.read32(m.bus.ctx, b + CLCD_WIN_FBADDR), FB);
    CHECK(m.bus.read32(m.bus.ctx, b + CLCD_WIN_GEOMETRY) == ((W << 16) | H),
          "window 0 geometry=%08x expect %08x",
          m.bus.read32(m.bus.ctx, b + CLCD_WIN_GEOMETRY), (W << 16) | H);
    CHECK(m.bus.read32(m.bus.ctx, b + CLCD_WIN_PITCH) == STRIDE, "stride");
    CHECK(m.bus.read32(m.bus.ctx, b + CLCD_WIN_LINEWORDS) == STRIDE / 4u, "line words");
    CHECK(((m.bus.read32(m.bus.ctx, b + CLCD_WIN_CONTROL) >> CLCD_FMT_SHIFT)
           & CLCD_FMT_MASK) == CLCD_FMT_32BPP, "pixel format");
    CHECK((m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_CTRL) & CLCD_CTRL_WIN0) != 0,
          "window 0 must be enabled in CLCD_CTRL");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_GATE) ==
              CLCD_N82_VIDCON0,
          "VIDCON0 must retain the N82 display clock divisor and scanout enable");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_STATUS) ==
              CLCD_N82_VIDCON1,
          "VIDCON1 must retain the N82 inverted-VCLK polarity");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_VIDTCON0) == 0x00030303u,
          "N82 vertical porch/sync timing");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_VIDTCON1) == 0x000e0e0fu,
          "N82 horizontal porch/sync timing");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_VIDTCON2) == 0x013f01dfu,
          "N82 active 320x480 timing");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_VIDTCON3) == 1u,
          "N82 VIDTCON3 handoff");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_WINCFG0) ==
              0x00001000u &&
          m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_WINCFG0 + 4u) == 0u &&
          m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_WINCFG0 + 8u) ==
              0x00001000u &&
          m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_WINCFG0 + 12u) == 0u &&
          m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_UPDATE2) ==
              0x00001000u &&
          m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_WINCFG2_AUX) == 0u,
          "openiBoot window-configuration handoff");
    CHECK(s5l_clcd_running(&m.clcd),
          "the seeded N82 display must be actively scanning");

    /* And the host-side accessor reports the same thing, which is the seam a
     * renderer reads. */
    uint32_t fb = 0, w = 0, h = 0, st = 0, fmt = 0, ord = 0;
    CHECK(s5l_clcd_window(&m.clcd, 0, &fb, &w, &h, &st, &fmt, &ord),
          "window 0 should report as enabled");
    CHECK(fb == FB && w == W && h == H && st == STRIDE &&
          fmt == CLCD_FMT_32BPP && ord == CLCD_ORDER_BGRA,
          "window 0 = {%08x %ux%u stride %u fmt %u order %u}", fb, w, h, st, fmt, ord);
    CHECK(!s5l_clcd_window(&m.clcd, 1, &fb, &w, &h, &st, &fmt, &ord),
          "window 1 is not enabled and must not report as if it were");

    /* Seeding must not be able to fire an interrupt at a guest that has not
     * asked for one: the mask is still zero. */
    s5l8900_tick(&m, m.cpu_hz);                 /* a whole guest second */
    CHECK(!m.cpu.irq_line, "a seeded display must not interrupt before the "
                           "guest enables the frame interrupt");
    CHECK(m.clcd.frames > 0, "scanout should have been running all the same");

    s5l8900_free(&m);
}

/* The host-side iBoot shim is a trust boundary: malformed or overflowed seed
 * data must be rejected atomically, never masked into plausible registers. */
static void test_clcd_seed_rejects_invalid_layouts_atomically(void) {
    s5l_clcd_t c; s5l_clcd_reset(&c);
    CHECK(s5l_clcd_seed_window0(&c, 0x0ff00000u, 320u, 480u, 1280u,
                                CLCD_FMT_32BPP, CLCD_ORDER_BGRA),
          "baseline seed failed");
    s5l_clcd_t before = c;

    struct {
        uint32_t fb, width, height, stride, format, order;
        const char *why;
    } bad[] = {
        { 0x0ff00000u, 0u,      480u, 1280u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "zero width" },
        { 0x0ff00000u, 0x401u,  480u, 8192u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "width field overflow" },
        { 0x0ff00000u, 320u,      0u, 1280u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "zero height" },
        { 0x0ff00000u, 320u,    0x400u, 1280u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "height field overflow" },
        { 0x0ff00000u, 320u,    480u, 1276u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "stride shorter than a row" },
        { 0x0ff00000u, 320u,    480u, 1282u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "stride not word aligned" },
        { 0x0ff00000u, 320u,    480u, 1280u, CLCD_FMT_MASK + 1u, CLCD_ORDER_BGRA,
          "format field overflow" },
        { 0x0ff00000u, 320u,    480u, 1280u, CLCD_FMT_32BPP, CLCD_ORDER_MASK + 1u,
          "order field overflow" },
        { 0xfffffff0u, 4u,        1u,   16u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "page-rounded physical span overflow" },
        { 0xffffe800u, 4u,        2u, 0x1000u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "padded final stride outside the physical address space" },
        { 0x00000000u, 1u,        2u, 0x80000000u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "stride-height multiplication overflow" },
        { 0x00000000u, 1u,        1u, 0xfffff004u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "page alignment overflow" },
        { 0xfffffff1u, 4u,        1u,   16u, CLCD_FMT_32BPP, CLCD_ORDER_BGRA,
          "physical address wrap" },
    };

    CHECK(!s5l_clcd_seed_window0(NULL, 0, 1, 1, 4,
                                 CLCD_FMT_32BPP, CLCD_ORDER_BGRA),
          "a null controller must be rejected");
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!s5l_clcd_seed_window0(&c, bad[i].fb, bad[i].width,
                                     bad[i].height, bad[i].stride,
                                     bad[i].format, bad[i].order),
              "accepted %s", bad[i].why);
        CHECK(memcmp(&c, &before, sizeof c) == 0,
              "%s changed controller state on rejection", bad[i].why);
    }

    s5l_clcd_t edge; s5l_clcd_reset(&edge);
    CHECK(s5l_clcd_seed_window0(&edge, 0xfffff000u, 4u, 1u, 16u,
                                CLCD_FMT_32BPP, CLCD_ORDER_BGRA),
          "a page-rounded framebuffer ending exactly at 4 GiB was rejected");
    CHECK(s5l_clcd_read(&edge, CLCD_VIDTCON2) == 0x00030000u,
          "non-native seed exposed fixed 320x480 active timing: %08x",
          s5l_clcd_read(&edge, CLCD_VIDTCON2));
}

/*
 * The window the DRIVER would pick, in the driver's own order. AppleH1CLCD
 * tests 0x40, then 0x20, then 0x10, then 0x08 (0xc0705f10..0xc0705f94) and
 * scans out the FIRST match; it does not merge windows and it does not prefer
 * the largest. Getting the order wrong here would put the picture in the wrong
 * place with no error anywhere.
 */
static void test_clcd_active_window_follows_the_drivers_order(void) {
    s5l_clcd_t c; s5l_clcd_reset(&c);
    CHECK(s5l_clcd_active_window(&c) == CLCD_WIN_NONE,
          "a controller with no window enabled must say so, not name window 0");

    s5l_clcd_write(&c, CLCD_CTRL, CLCD_CTRL_WIN2 | CLCD_CTRL_WIN3);
    CHECK(s5l_clcd_active_window(&c) == 2,
          "with windows 2 and 3 enabled the driver takes 2, got %u",
          s5l_clcd_active_window(&c));

    s5l_clcd_write(&c, CLCD_CTRL, CLCD_CTRL_WIN1 | CLCD_CTRL_WIN2 | CLCD_CTRL_WIN3);
    CHECK(s5l_clcd_active_window(&c) == 1, "window 1 outranks 2 and 3");

    s5l_clcd_write(&c, CLCD_CTRL, 0xffu);
    CHECK(s5l_clcd_active_window(&c) == 0, "window 0 outranks everything");

    /* CLCD_CTRL_VIDEO is not an RGB window and must not be mistaken for one. */
    s5l_clcd_write(&c, CLCD_CTRL, CLCD_CTRL_VIDEO | CLCD_CTRL_ENABLE);
    CHECK(s5l_clcd_active_window(&c) == CLCD_WIN_NONE,
          "the video overlay bit is not an RGB window");
}

/*
 * A remembered window is not necessarily live scanout. Power-management paths
 * stop the controller by clearing any of three independent gates; stale seeded
 * pixels must not keep producing VBLs or be advertised as a running display.
 */
static void test_clcd_running_requires_every_scanout_gate(void) {
    s5l_clcd_t c;
    s5l_clcd_reset(&c);
    c.frame_ticks = 4;
    CHECK(s5l_clcd_seed_window0(&c, 0x0ff00000u, 320u, 480u, 1280u,
                                CLCD_FMT_32BPP, CLCD_ORDER_BGRA),
          "baseline seed failed");
    CHECK(s5l_clcd_running(&c), "seeded controller is not running");

    s5l_clcd_t stopped = c;
    stopped.scanning = false;
    CHECK(!s5l_clcd_running(&stopped), "start/stop state was ignored");
    stopped = c;
    stopped.ctrl &= ~CLCD_CTRL_ENABLE;
    CHECK(!s5l_clcd_running(&stopped), "CLCD_CTRL global enable was ignored");
    stopped = c;
    stopped.gate &= ~1u;
    CHECK(!s5l_clcd_running(&stopped), "VIDCON0 scanout gate was ignored");
    CHECK(s5l_clcd_active_window(&stopped) == 0,
          "stopping scanout must not erase the driver's remembered window");

    stopped.intmask = CLCD_INT_FRAME;
    stopped.intstatus = 0;
    stopped.frame_accum = 0;
    CHECK(!s5l_clcd_tick(&stopped, stopped.frame_ticks),
          "a gated-off controller asserted an interrupt");
    CHECK(stopped.frames == 0 && stopped.intstatus == 0,
          "a gated-off controller advanced a frame");
}

/*
 * Scanout: the pixels the guest wrote are the pixels that come out, and a
 * window that is programmed wrong produces an ERROR rather than a picture.
 */
static void test_clcd_scanout_reads_guest_memory(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0x08000000u, 1u << 20), "machine init failed");

    const uint32_t W = 4, H = 3, STRIDE = 64;       /* stride > W*bpp on purpose */
    const uint32_t FB = 0x08000000u + 0x1000u;
    s5l_clcd_seed_window0(&m.clcd, FB, W, H, STRIDE,
                          CLCD_FMT_32BPP, CLCD_ORDER_BGRA);

    /* Paint a known pattern through the bus, as the guest would. Stored as
     * 0xAARRGGBB, so byte 0 is blue and byte 2 is red. */
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            m.bus.write32(m.bus.ctx, FB + y * STRIDE + x * 4u,
                          0xff000000u | ((x * 16u) << 16) | ((y * 32u) << 8) | 7u);

    uint8_t rgb[4 * 3 * 3];
    uint32_t ow = 0, oh = 0;
    CHECK(s5l_clcd_scanout(&m.clcd, 0, m.ram, m.ram_base, m.ram_size,
                           rgb, sizeof rgb, &ow, &oh),
          "scanout of a correctly programmed window must succeed");
    CHECK(ow == W && oh == H, "scanout geometry %ux%u, expected %ux%u", ow, oh, W, H);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            const uint8_t *p = &rgb[(y * W + x) * 3];
            CHECK(p[0] == (uint8_t)(x * 16u) && p[1] == (uint8_t)(y * 32u) && p[2] == 7,
                  "pixel (%u,%u) = %02x%02x%02x", x, y, p[0], p[1], p[2]);
        }

    /* A buffer one byte short must be refused, not overrun. */
    CHECK(!s5l_clcd_scanout(&m.clcd, 0, m.ram, m.ram_base, m.ram_size,
                            rgb, sizeof rgb - 1u, NULL, NULL),
          "a short destination must be an error");

    /* A window whose last row runs past the end of DRAM must be refused. Placing
     * the base so that h-1 strides plus one row overshoots by a single byte is
     * the case an off-by-one would wave through. */
    uint32_t last = (uint32_t)(m.ram_base + m.ram_size);
    uint32_t need = (H - 1u) * STRIDE + W * 4u;
    s5l_clcd_seed_window0(&m.clcd, last - need + 1u, W, H, STRIDE,
                          CLCD_FMT_32BPP, CLCD_ORDER_BGRA);
    CHECK(!s5l_clcd_scanout(&m.clcd, 0, m.ram, m.ram_base, m.ram_size,
                            rgb, sizeof rgb, NULL, NULL),
          "a window running one byte past DRAM must be an error, not a picture");

    /* A stride too small to hold a row is a programming fault, not a squeeze. */
    /* Guest MMIO can still produce malformed state even though the host seed
     * API rejects it, so exercise that path through the real register write. */
    s5l_clcd_write(&m.clcd, CLCD_WIN_FIRST + CLCD_WIN_PITCH, W * 4u - 1u);
    CHECK(!s5l_clcd_scanout(&m.clcd, 0, m.ram, m.ram_base, m.ram_size,
                            rgb, sizeof rgb, NULL, NULL),
          "a stride shorter than one row must be an error");

    /* A disabled window has nothing to scan out. */
    s5l_clcd_seed_window0(&m.clcd, FB, W, H, STRIDE,
                          CLCD_FMT_32BPP, CLCD_ORDER_BGRA);
    s5l_clcd_write(&m.clcd, CLCD_CTRL, 0);
    CHECK(!s5l_clcd_scanout(&m.clcd, 0, m.ram, m.ram_base, m.ram_size,
                            rgb, sizeof rgb, NULL, NULL),
          "a disabled window must not scan out");

    s5l8900_free(&m);
}

/*
 * 16-bit windows. The driver publishes '565L' for every format below 6, so a
 * 16-bit window is 5-6-5 — and full-scale in has to be full-scale out, which a
 * plain left-shift does not give you (0x1f << 3 is 0xf8, not 0xff).
 */
static void test_clcd_scanout_565_reaches_full_white(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0x08000000u, 1u << 20), "machine init failed");

    const uint32_t W = 2, H = 1, STRIDE = 8, FB = 0x08000000u + 0x2000u;
    s5l_clcd_seed_window0(&m.clcd, FB, W, H, STRIDE,
                          CLCD_FMT_RGB565, CLCD_ORDER_BGRA);
    /* pixel 0 = white (all bits set), pixel 1 = pure green at full scale. */
    m.bus.write32(m.bus.ctx, FB, 0x07e0ffffu);

    uint8_t rgb[2 * 3];
    CHECK(s5l_clcd_scanout(&m.clcd, 0, m.ram, m.ram_base, m.ram_size,
                           rgb, sizeof rgb, NULL, NULL), "565 scanout");
    CHECK(rgb[0] == 0xff && rgb[1] == 0xff && rgb[2] == 0xff,
          "0xffff must decode to full white, got %02x%02x%02x",
          rgb[0], rgb[1], rgb[2]);
    CHECK(rgb[3] == 0 && rgb[4] == 0xff && rgb[5] == 0,
          "0x07e0 must decode to full green, got %02x%02x%02x",
          rgb[3], rgb[4], rgb[5]);

    s5l8900_free(&m);
}

/*
 * The whole path, end to end: the guest arms the frame interrupt exactly the
 * way AppleH1CLCD's swap_submit does, and the CPU's IRQ line comes up through
 * VIC line 13 — the line the device tree gives /arm-io/clcd.
 */
static void test_clcd_interrupt_reaches_the_cpu_on_line_13(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE, 1u << S5L8900_IRQ_CLCD);
    /* swap_submit: clear stale status, then enable the frame interrupt. */
    m.bus.write32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_INTSTATUS, CLCD_INT_FRAME);
    m.bus.write32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_INTMASK,
                  CLCD_INT_UNDERRUN | CLCD_INT_FRAME);
    m.bus.write32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_CTRL, CLCD_CTRL_ENABLE);
    m.bus.write32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_GATE, 1u);
    m.bus.write32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_ENABLE, 1);

    /* One guest frame of instructions at the real CPU:timebase ratio. */
    uint32_t insns = (uint32_t)((uint64_t)m.cpu_hz / S5L_CLCD_REFRESH_HZ) + 16u;
    for (uint32_t i = 0; i < insns; i++) s5l8900_tick(&m, 1);

    CHECK(m.cpu.irq_line,
          "no IRQ after a guest frame — swap_submit would never complete");
    uint32_t st = m.bus.read32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_INTSTATUS);
    CHECK((st & CLCD_INT_FRAME) != 0, "status=%08x expect the frame bit", st);
    CHECK((st & CLCD_INT_UNDERRUN) == 0, "status=%08x must never report underrun", st);
    CHECK((m.bus.read32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_IRQSTATUS) &
           (1u << S5L8900_IRQ_CLCD)) != 0,
          "the CLCD must present on VIC line 13, as the device tree says");

    /* Acknowledge as the handler does, and the line must drop. */
    m.bus.write32(m.bus.ctx, S5L8900_CLCD_BASE + CLCD_INTSTATUS, CLCD_INT_FRAME);
    s5l8900_tick(&m, 1);
    CHECK(!m.cpu.irq_line, "the IRQ must drop once the frame bit is acknowledged");

    printf("  [CLCD] %llu frames in one guest frame period, VIC line %u\n",
           (unsigned long long)m.clcd.frames, S5L8900_IRQ_CLCD);
    s5l8900_free(&m);
}

/*
 * VICADDRESS (0xF00) is how AppleARMPL192VIC finds the pending source — it does
 * NOT read IRQSTATUS. It decodes the returned word as source|0x80000000 and
 * dispatches. Returning a bare 0 (no valid tag) decodes to spurious source 0,
 * so a real interrupt is acknowledged without its handler running and re-fires
 * forever — the boot-stopping storm the self-IPI on line 4 produced.
 */
static void test_vic_vectaddr_reports_tagged_source(void) {
    s5l_vic_t v; s5l_vic_reset(&v);

    /* Nothing pending -> 0, which the driver correctly reads as spurious. */
    CHECK(s5l_vic_vectaddr(&v, 0) == 0, "idle VICADDRESS must be 0");

    /* Reproduce the storm exactly: software interrupt bit 4, enabled, routed to
     * IRQ (not selected to FIQ) — the self-IPI. */
    s5l_vic_write(&v, VIC_SOFTINT, 1u << 4);
    s5l_vic_write(&v, VIC_INTENABLE, 1u << 4);
    CHECK(s5l_vic_vectaddr(&v, 0) == (0x80000000u | 4u),
          "VICADDRESS=%08x expect 80000004 (source 4, valid bit) — a bare 0 "
          "here is the storm", s5l_vic_vectaddr(&v, 0));

    /* The driver then clears it via SOFTINTCLEAR, and it must go idle. */
    s5l_vic_write(&v, VIC_SOFTINTCLEAR, 1u << 4);
    CHECK(s5l_vic_vectaddr(&v, 0) == 0,
          "after SOFTINTCLEAR the source must be gone, not re-reported");

    /* base_source places a VIC in the daisy chain: VIC1's line 4 is global 36. */
    s5l_vic_reset(&v);
    s5l_vic_write(&v, VIC_SOFTINT, 1u << 4);
    s5l_vic_write(&v, VIC_INTENABLE, 1u << 4);
    CHECK(s5l_vic_vectaddr(&v, 32) == (0x80000000u | 36u),
          "VIC1 line 4 must report as global source 36");

    /* Lowest-numbered pending line wins (default priority). */
    s5l_vic_reset(&v);
    s5l_vic_write(&v, VIC_INTENABLE, 0xffffffffu);
    s5l_vic_set_line(&v, 9, true);
    s5l_vic_set_line(&v, 3, true);
    CHECK(s5l_vic_vectaddr(&v, 0) == (0x80000000u | 3u),
          "lowest pending line (3) must be selected, got %08x",
          s5l_vic_vectaddr(&v, 0));

    /* A line routed to FIQ must not appear in VICADDRESS (which is IRQ-only). */
    s5l_vic_reset(&v);
    s5l_vic_write(&v, VIC_INTENABLE, 1u << 7);
    s5l_vic_write(&v, VIC_INTSELECT, 1u << 7);
    s5l_vic_set_line(&v, 7, true);
    CHECK(s5l_vic_vectaddr(&v, 0) == 0,
          "an FIQ-routed line must not surface through VICADDRESS");
}

static void test_vic_masks_and_routes(void) {
    s5l_vic_t v; s5l_vic_reset(&v);
    s5l_vic_set_line(&v, 5, true);
    CHECK(!s5l_vic_irq(&v), "line asserted but disabled should not raise IRQ");
    s5l_vic_write(&v, VIC_INTENABLE, 1u << 5);
    CHECK(s5l_vic_irq(&v), "enabled line should raise IRQ");
    s5l_vic_write(&v, VIC_INTSELECT, 1u << 5);
    CHECK(!s5l_vic_irq(&v) && s5l_vic_fiq(&v), "select should route the line to FIQ");
    s5l_vic_write(&v, VIC_INTSELECT, 0);
    s5l_vic_write(&v, VIC_INTENCLEAR, 1u << 5);
    CHECK(!s5l_vic_irq(&v), "cleared enable should drop IRQ");
}

/*
 * The full interrupt path: timer counts down -> VIC masks/routes it -> the CPU
 * takes an IRQ exception -> the guest handler prints 'T' and returns with
 * SUBS pc, lr, #4, landing back in the interrupted spin loop.
 *
 *   0x18: B 0x40          ; IRQ vector
 *   0x40: MOV r1,#'T'
 *   0x44: STR r1,[r0,#0x20]
 *   0x48: MOV r1,#0x00030000
 *   0x4c: STR r1,[r2,#0xf4]  ; acknowledge, exactly as the kernel's handler does
 *   0x50: SUBS pc,lr,#4      ; return from IRQ (restores CPSR from SPSR)
 *   0x100: B .               ; main loop
 */
static void test_timer_interrupt_reaches_handler(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    /* B 0x40 from 0x18: target = pc + 8 + off  ->  0x40 = 0x18 + 8 + off */
    const uint32_t branch = 0xea000000u | (((0x40u - 0x18u - 8u) / 4u) & 0x00ffffffu);
    s5l8900_load(&m, 0x18, &branch, 4);

    /* The handler disables the timer so exactly one interrupt fires; that lets
     * us assert precisely where the CPU ends up after returning. */
    const uint32_t handler[] = {
        0xe3a01054u,   /* MOV r1,#0x54 'T'      */
        0xe5801020u,   /* STR r1,[r0,#0x20]     */
        0xe3a01000u,   /* MOV r1,#0             */
        0xe58210a4u,   /* STR r1,[r2,#0xa4]  stop timer 4 */
        0xe3a01803u,   /* MOV r1,#0x00030000    */
        0xe58210f4u,   /* STR r1,[r2,#0xf4]  acknowledge */
        0xe25ef004u    /* SUBS pc,lr,#4      return from IRQ */
    };
    s5l8900_load(&m, 0x40, handler, sizeof handler);

    const uint32_t spin = 0xeafffffeu;              /* B . */
    s5l8900_load(&m, 0x100, &spin, 4);

    /* This test exercises device -> controller -> CPU, not the clock ratio, so
     * run the timebase at one tick per instruction to keep it readable. */
    m.cpu_hz = m.tb_hz = 1;

    /* Program the controller and timer the way guest setup code would. */
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE, 1u << S5L8900_IRQ_TIMER);
    m.bus.write32(m.bus.ctx, S5L8900_TIMER_BASE + TIMER4_COUNTBUF, 4);
    m.bus.write32(m.bus.ctx, S5L8900_TIMER_BASE + TIMER4_STATE,
                  TIMER4_STATE_START | TIMER4_STATE_UPDATE);

    /* Start in SYS mode with IRQs unmasked, spinning at 0x100. */
    m.cpu.r[15] = 0x100;
    m.cpu.r[0]  = S5L8900_UART0_BASE;
    m.cpu.r[2]  = S5L8900_TIMER_BASE;
    m.cpu.cpsr  = ARM_MODE_SYS;                     /* I and F clear */

    arm_status_t st = ARM_OK;
    s5l8900_run(&m, 200, &st);

    CHECK(st == ARM_OK, "status=%d expect ARM_OK", (int)st);
    m.uart0.tx[m.uart0.tx_len] = '\0';
    CHECK(strcmp(m.uart0.tx, "T") == 0,
          "uart=\"%s\" expect exactly one 'T' (handler ran once)", m.uart0.tx);
    /* Having returned via SUBS pc,lr,#4 we must be back in the interrupted
     * mode, at the interrupted instruction. */
    CHECK((m.cpu.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SYS,
          "mode=%02x expect back in SYS after IRQ return",
          m.cpu.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(m.cpu.r[15] == 0x100, "pc=%08x expect 100 (resumed the spin loop)",
          m.cpu.r[15]);
    printf("  [timer IRQ -> handler -> return] uart=\"%s\", resumed at pc=%08x\n",
           m.uart0.tx, m.cpu.r[15]);
    s5l8900_free(&m);
}

int main(void) {
    printf("S5LBox S5L8900 machine tests\n");
    test_ram_readback();
    test_direct_ram_write_consent_is_fail_closed();
    test_pre_step_hook_is_bounded_and_fail_closed();
    test_uart_status_is_ready();
    test_unmapped_access_counted();
    test_bounds_check_cannot_overflow();
    test_bare_metal_uart_hello();
    test_stub_window_stores_and_counts();
    test_mmio_width_alignment_and_window_edges();
    test_address_space_wrap_is_refused();
    test_machine_declares_its_known_windows();
    test_baseband_spi_window_reads_back_its_configuration();
    test_nor_reads_are_nor_at_the_boot_ram_size();
    test_no_window_the_machine_decodes_is_shadowed_by_ram();
    test_the_nor_window_is_out_of_every_drams_reach();
    test_soc_regions_match_the_device_tree();
    test_gpio_base_is_the_s5l8900_address();
    test_power_gate_state_tracks_onctrl_offctrl();
    test_usb_otg_config_registers_read_their_specified_values();
    test_usb_otg_pcgcctl_round_trips_the_drivers_sequence();
    test_usb_otg_endpoint_derivation_is_self_consistent();
    test_vic_vectaddr_reports_tagged_source();
    test_vic_masks_and_routes();
    test_vic1_is_mapped_and_drives_the_cpu();
    test_tvout_register_lanes_and_idle_handshake();
    test_tvout_run19_resume_state_and_control_phase();
    test_tvout_vsync_w1c_mask_and_phase();
    test_tvout_machine_routing_and_irq30();
    test_tvout_wfi_fast_forwards_to_vsync();
    test_clcd_raises_the_frame_interrupt();
    test_clcd_large_tick_preserves_phase();
    test_clcd_line_is_gated_by_the_mask();
    test_clcd_status_never_defers_the_swap();
    test_clcd_saved_registers_read_back();
    test_clcd_counts_only_real_window_updates();
    test_clcd_seed_is_visible_to_the_guest();
    test_clcd_seed_rejects_invalid_layouts_atomically();
    test_clcd_active_window_follows_the_drivers_order();
    test_clcd_running_requires_every_scanout_gate();
    test_clcd_scanout_reads_guest_memory();
    test_clcd_scanout_565_reaches_full_white();
    test_clcd_interrupt_reaches_the_cpu_on_line_13();
    test_timebase_runs_without_a_timer();
    test_timebase_runs_at_the_guest_ratio();
    test_skipped_refresh_is_invisible_to_the_guest();
    test_run_tick_path_matches_public_contract();
    test_mbx_edram_owns_and_declares_the_full_aperture();
    test_timer_period_is_exact();
    test_timer_ack_mask_matches_the_kernels();
    test_timer_lump_matches_literal_countdown();
    test_wfi_fast_forwards_to_the_timer_boundary();
    test_privileged_batch_flushes_before_wfi();
    test_wfi_host_pacing_is_optional_exact_and_yields();
    test_wfi_host_pacing_bounds_long_and_failed_waits();
    test_active_host_clock_is_optional_bounded_and_fail_closed();
    test_active_host_clock_does_not_double_count_paced_wfi();
    test_active_host_clock_preserves_only_bounded_wfi_oversleep();
    test_active_host_clock_refreshes_devices_without_oversampling();
    test_wfi_unmasked_fiq_uses_the_post_mcr_return_link();
    test_wfi_pending_line_completes_without_advancing_time();
    test_wfi_stops_at_earliest_deliverable_device_edge();
    test_wfi_lump_preserves_non_waking_device_side_effects();
    test_wfi_no_event_falls_back_without_time_or_host_hang();
    test_wfi_existing_source_edges_are_unchanged();
    test_wfi_nearer_wake_source_wins();
    test_wfi_never_source_is_ignored_not_treated_as_zero();
    test_wfi_unpredictable_source_stops_the_fast_forward();
    test_wfi_wake_source_order_does_not_matter();
    test_timer_interrupt_reaches_handler();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
