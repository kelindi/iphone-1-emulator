/*
 * S5LBox — ARMv6 interpreter unit tests.
 *
 * A tiny dependency-free test harness: a flat 1 MiB RAM behind the arm_bus_t,
 * hand-assembled ARM encodings, single-stepped, with register/flag assertions.
 * Runs identically on Windows (MinGW), Linux, and macOS CI.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "arm.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------- flat memory */
#define RAM_SIZE (1u << 20)
static uint8_t g_ram[RAM_SIZE];

/* Optional one-address bus watch used to prove that privilege/fault rejection
 * happens before a device-like read or write. Disabled for ordinary tests. */
static uint32_t g_watch_addr = 0xffffffffu;
static unsigned g_watch_reads32, g_watch_writes32;
static unsigned g_watch_reads16, g_watch_writes16;
static unsigned g_watch_reads8, g_watch_writes8;

static uint32_t m_r32(void *ctx, uint32_t a){ (void)ctx; uint32_t v; if(a==g_watch_addr)g_watch_reads32++; memcpy(&v,&g_ram[a&(RAM_SIZE-1)],4); return v; }
static uint16_t m_r16(void *ctx, uint32_t a){ (void)ctx; uint16_t v; if(a==g_watch_addr)g_watch_reads16++; memcpy(&v,&g_ram[a&(RAM_SIZE-1)],2); return v; }
static uint8_t  m_r8 (void *ctx, uint32_t a){ (void)ctx; if(a==g_watch_addr)g_watch_reads8++; return g_ram[a&(RAM_SIZE-1)]; }
static void m_w32(void *ctx, uint32_t a, uint32_t v){ (void)ctx; if(a==g_watch_addr)g_watch_writes32++; memcpy(&g_ram[a&(RAM_SIZE-1)],&v,4); }
static void m_w16(void *ctx, uint32_t a, uint16_t v){ (void)ctx; if(a==g_watch_addr)g_watch_writes16++; memcpy(&g_ram[a&(RAM_SIZE-1)],&v,2); }
static void m_w8 (void *ctx, uint32_t a, uint8_t  v){ (void)ctx; if(a==g_watch_addr)g_watch_writes8++; g_ram[a&(RAM_SIZE-1)]=v; }

static uint8_t *m_host_ram_write(void *ctx, uint32_t a, uint32_t len) {
    (void)ctx;
    if (!len || (uint64_t)a + len > RAM_SIZE) return NULL;
    return &g_ram[a];
}

static uint8_t *m_host_ram(void *ctx, uint32_t a, uint32_t len) {
    return m_host_ram_write(ctx, a, len);
}

/* Designated, so a new optional hook on arm_bus_t cannot break this file: the
 * positional form listed ten members and the struct has grown four. Every
 * omitted member is a NULL optional hook, which is what the trailing NULLs
 * meant. See test_jit.c for how this stayed red on one CI job for a dozen
 * commits. */
static const arm_bus_t g_bus = {
    .ctx     = NULL,
    .read32  = m_r32, .read16  = m_r16, .read8  = m_r8,
    .write32 = m_w32, .write16 = m_w16, .write8 = m_w8,
};

static bool count_wfi(void *ctx) {
    unsigned *calls = ctx;
    (*calls)++;
    return false;                 /* exercise the core's non-blocking fallback */
}

typedef struct svc_probe {
    arm_svc_result_t result;
    arm_cpu_t       *expected_cpu;
    unsigned         calls;
    uint32_t         seen_pc;
    uint32_t         seen_encoding;
    uint32_t         seen_cpsr;
    uint32_t         seen_r0;
    uint32_t         seen_cpu_pc;
    bool             saw_expected_cpu;
    bool             mutate;
    bool             redirect;
    bool             redirect_thumb;
    uint32_t         redirect_pc;
} svc_probe_t;

static arm_svc_result_t probe_privileged_svc(void *ctx, arm_cpu_t *cpu,
                                              uint32_t pc,
                                              uint32_t encoding) {
    svc_probe_t *probe = ctx;
    probe->calls++;
    probe->seen_pc = pc;
    probe->seen_encoding = encoding;
    probe->seen_cpsr = cpu->cpsr;
    probe->seen_r0 = cpu->r[0];
    probe->seen_cpu_pc = cpu->r[15];
    probe->saw_expected_cpu = cpu == probe->expected_cpu;

    if (probe->mutate) {
        cpu->r[0] = 0xfeed0001u;
        cpu->r[7] = 0xfeed0007u;
        cpu->r[15] = 0xfeed0015u;
        cpu->cp15.context_id = 0xfeedc013u;
        cpu->cpsr ^= ARM_CPSR_N;
        cpu->excl_valid = false;
        cpu->excl_addr = 0xfeedeeeeu;
    }
    if (probe->redirect) {
        cpu->r[15] = probe->redirect_pc;
        if (probe->redirect_thumb) cpu->cpsr |= ARM_CPSR_T;
        else                       cpu->cpsr &= ~ARM_CPSR_T;
    }
    return probe->result;
}

/* ------------------------------------------------------------- test runner */
static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* Load a sequence of instruction words at 0 and run n steps from PC=0. */
static void load_and_run(arm_cpu_t *c, const uint32_t *prog, size_t words, int steps) {
    memset(g_ram, 0, sizeof g_ram);
    for (size_t i = 0; i < words; i++) m_w32(NULL, (uint32_t)(i*4), prog[i]);
    arm_reset(c, &g_bus);
    c->cpsr = (c->cpsr & ~0x1fu) | ARM_MODE_SYS; /* SYS so all regs are flat */
    for (int i = 0; i < steps; i++) if (arm_step(c) != ARM_OK) break;
}

/* Like load_and_run but returns the status of the last step (for trap tests). */
static arm_status_t run_status(arm_cpu_t *c, const uint32_t *prog, size_t words, int steps) {
    memset(g_ram, 0, sizeof g_ram);
    for (size_t i = 0; i < words; i++) m_w32(NULL, (uint32_t)(i*4), prog[i]);
    arm_reset(c, &g_bus);
    c->cpsr = (c->cpsr & ~0x1fu) | ARM_MODE_SYS;
    arm_status_t st = ARM_OK;
    for (int i = 0; i < steps; i++) { st = arm_step(c); if (st != ARM_OK) break; }
    return st;
}

/* --------------------------------------------------------------- the tests */

static void test_mov_imm(void) {
    /* MOV r0, #42  -> e3a0002a */
    uint32_t p[] = { 0xe3a0002a };
    arm_cpu_t c; load_and_run(&c, p, 1, 1);
    CHECK(c.r[0] == 42, "r0=%u expect 42", c.r[0]);
    CHECK(c.r[15] == 4, "pc=%u expect 4", c.r[15]);
}

static void test_add_reg(void) {
    /* MOV r1,#40 ; MOV r2,#2 ; ADD r0,r1,r2 */
    uint32_t p[] = { 0xe3a01028, 0xe3a02002, 0xe0810002 };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.r[0] == 42, "r0=%u expect 42", c.r[0]);
}

static void test_sub_flags(void) {
    /* MOV r0,#5 ; SUBS r0,r0,#5  -> Z set, C set (no borrow) */
    uint32_t p[] = { 0xe3a00005, 0xe2500005 };
    arm_cpu_t c; load_and_run(&c, p, 2, 2);
    CHECK(c.r[0] == 0, "r0=%u expect 0", c.r[0]);
    CHECK((c.cpsr & ARM_CPSR_Z) != 0, "Z should be set");
    CHECK((c.cpsr & ARM_CPSR_C) != 0, "C should be set (no borrow)");
    CHECK((c.cpsr & ARM_CPSR_N) == 0, "N should be clear");
}

static void test_subs_negative(void) {
    /* MOV r0,#1 ; SUBS r0,r0,#2 -> result 0xffffffff, N set, C clear (borrow) */
    uint32_t p[] = { 0xe3a00001, 0xe2500002 };
    arm_cpu_t c; load_and_run(&c, p, 2, 2);
    CHECK(c.r[0] == 0xffffffffu, "r0=%08x expect ffffffff", c.r[0]);
    CHECK((c.cpsr & ARM_CPSR_N) != 0, "N should be set");
    CHECK((c.cpsr & ARM_CPSR_C) == 0, "C should be clear (borrow)");
}

static void test_adds_overflow(void) {
    /* r0 = 0x7fffffff ; ADDS r0,r0,#1 -> V set, N set */
    /* MOV r0,#0xff000000-ish is awkward; build 0x7fffffff via MVN r0,#0x80000000? */
    /* Simpler: MOV r0,#0x7f000000 rotated... use two: MVN r0,#0x80000000 gives 0x7fffffff */
    /* MVN r0, #0x80000000 : imm=0x80000000 not encodable directly; use ror.
       0x80000000 = 0x2 ror 2? 0x02 rotated right by 2 -> 0x80000000. rot field=1 (=2*1). */
    uint32_t p[] = { 0xe3e00102, /* MVN r0,#0x80000000 -> r0=0x7fffffff */
                     0xe2900001  /* ADDS r0,r0,#1 */ };
    arm_cpu_t c; load_and_run(&c, p, 2, 2);
    CHECK(c.r[0] == 0x80000000u, "r0=%08x expect 80000000", c.r[0]);
    CHECK((c.cpsr & ARM_CPSR_V) != 0, "V should be set on signed overflow");
    CHECK((c.cpsr & ARM_CPSR_N) != 0, "N should be set");
}

static void test_nzcv_updates_preserve_the_rest_of_cpsr(void) {
    static const struct {
        const char *name;
        uint32_t insn, a, b;
        uint32_t addend, cin;
        bool input_carry;
    } arithmetic[] = {
        /* ADDS r0,r1,r2: positive overflow without carry. */
        { "ADDS", 0xe0910002u, 0x7fffffffu, 1u,          1u,          0u, false },
        /* ADCS r0,r1,r2: the incoming carry produces zero and carry-out. */
        { "ADCS", 0xe0b10002u, 0xffffffffu, 0u,          0u,          1u, true  },
        /* SUBS r0,r1,r2 is implemented as a + ~b + 1. */
        { "SUBS", 0xe0510002u, 0u,          1u,          0xfffffffeu, 1u, false },
        /* SBCS with C clear subtracts the extra one and crosses INT32_MIN. */
        { "SBCS", 0xe0d10002u, 0x80000000u, 0u,          0xffffffffu, 0u, false },
    };
    const uint32_t nzcv_mask = ARM_CPSR_N | ARM_CPSR_Z |
                               ARM_CPSR_C | ARM_CPSR_V;
    const uint32_t preserved = ARM_MODE_SYS | ARM_CPSR_Q | ARM_CPSR_A |
                               ARM_CPSR_E | ARM_CPSR_I | ARM_CPSR_F |
                               0x000f0000u; /* GE[3:0] */

    for (size_t i = 0; i < sizeof arithmetic / sizeof arithmetic[0]; i++) {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0u, arithmetic[i].insn);
        arm_cpu_t c;
        arm_reset(&c, &g_bus);
        c.cpsr = preserved | ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_V |
                 (arithmetic[i].input_carry ? ARM_CPSR_C : 0u);
        c.r[1] = arithmetic[i].a;
        c.r[2] = arithmetic[i].b;

        uint64_t wide = (uint64_t)arithmetic[i].a +
                        (uint64_t)arithmetic[i].addend + arithmetic[i].cin;
        int64_t signed_wide = (int64_t)(int32_t)arithmetic[i].a +
                              (int64_t)(int32_t)arithmetic[i].addend +
                              arithmetic[i].cin;
        uint32_t result = (uint32_t)wide;
        uint32_t expected_flags = (result & ARM_CPSR_N)
                                | (result == 0u ? ARM_CPSR_Z : 0u)
                                | ((wide >> 32) != 0u ? ARM_CPSR_C : 0u)
                                | (signed_wide > 2147483647ll ||
                                   signed_wide < -2147483648ll ? ARM_CPSR_V : 0u);

        CHECK(arm_step(&c) == ARM_OK && c.r[0] == result,
              "%s result=%08x expect %08x", arithmetic[i].name, c.r[0], result);
        CHECK(c.cpsr == (preserved | expected_flags),
              "%s CPSR=%08x expect %08x (changed non-NZCV bits=%08x)",
              arithmetic[i].name, c.cpsr, preserved | expected_flags,
              (c.cpsr ^ preserved) & ~nzcv_mask);
    }

    static const struct {
        const char *name;
        uint32_t insn, r1, r2, result, nzc;
        bool input_carry;
    } logical[] = {
        /* MOVS r0,r2,LSL #1 takes C from bit 31 of r2. */
        { "MOVS zero", 0xe1b00082u, 0u, 0x80000000u, 0u,
          ARM_CPSR_Z | ARM_CPSR_C, false },
        { "MOVS negative", 0xe1b00082u, 0u, 0x40000000u, 0x80000000u,
          ARM_CPSR_N, true },
        /* ANDS with LSL #0 preserves the incoming shifter carry. */
        { "ANDS", 0xe0110002u, 0xf0u, 0x0fu, 0u,
          ARM_CPSR_Z | ARM_CPSR_C, true },
    };

    for (size_t i = 0; i < sizeof logical / sizeof logical[0]; i++) {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0u, logical[i].insn);
        arm_cpu_t c;
        arm_reset(&c, &g_bus);
        c.cpsr = preserved | ARM_CPSR_V |
                 ((~logical[i].nzc) & (ARM_CPSR_N | ARM_CPSR_Z)) |
                 (logical[i].input_carry ? ARM_CPSR_C : 0u);
        c.r[1] = logical[i].r1;
        c.r[2] = logical[i].r2;

        CHECK(arm_step(&c) == ARM_OK && c.r[0] == logical[i].result,
              "%s result=%08x expect %08x", logical[i].name,
              c.r[0], logical[i].result);
        CHECK(c.cpsr == (preserved | ARM_CPSR_V | logical[i].nzc),
              "%s CPSR=%08x expect %08x", logical[i].name, c.cpsr,
              preserved | ARM_CPSR_V | logical[i].nzc);
    }
}

static void test_barrel_lsl(void) {
    /* MOV r1,#1 ; MOV r0, r1, LSL #4 -> 16 */
    uint32_t p[] = { 0xe3a01001, 0xe1a00201 };
    arm_cpu_t c; load_and_run(&c, p, 2, 2);
    CHECK(c.r[0] == 16, "r0=%u expect 16", c.r[0]);
}

static void test_register_shifted_pc_operands_are_unpredictable(void) {
    static const struct { uint32_t insn; const char *what; } cases[] = {
        { 0xe1a0011fu, "Rm=pc" }, /* MOV r0,pc,LSL r1 */
        { 0xe1a00f11u, "Rs=pc" }, /* MOV r0,r1,LSL pc */
        { 0xe08f0211u, "Rn=pc" }, /* ADD r0,pc,r1,LSL r2 */
        { 0xe080f211u, "Rd=pc" }, /* ADD pc,r0,r1,LSL r2 */
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        arm_cpu_t c;
        uint32_t before[16], cpsr;
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0u, cases[i].insn);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS | ARM_CPSR_C;
        c.r[0] = 0x11111111u; c.r[1] = 3u; c.r[2] = 1u; c.r[15] = 0u;
        memcpy(before, c.r, sizeof before);
        cpsr = c.cpsr;
        CHECK(arm_step(&c) == ARM_UNDEFINED,
              "register-shifted data processing with %s must trap", cases[i].what);
        CHECK(memcmp(before, c.r, sizeof before) == 0 && c.cpsr == cpsr,
              "register-shifted %s changed registers or flags before trapping",
              cases[i].what);
    }
}

static void test_str_and_stm_store_pc_plus_12(void) {
    arm_cpu_t c;

    /* STR pc,[r0] at 0x100 stores 0x10c, not the ordinary visible PC 0x108. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x100u, 0xe580f000u);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.r[0] = 0x800u;
    c.r[15] = 0x100u;
    CHECK(arm_step(&c) == ARM_OK, "STR pc should execute");
    CHECK(m_r32(NULL, 0x800u) == 0x10cu,
          "STR pc stored %08x expect 0000010c", m_r32(NULL, 0x800u));

    /* The same exceptional PC value applies when r15 appears in an STM list. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x100u, 0xe8808000u);             /* STMIA r0,{pc} */
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.r[0] = 0x800u;
    c.r[15] = 0x100u;
    CHECK(arm_step(&c) == ARM_OK, "STMIA {pc} should execute");
    CHECK(m_r32(NULL, 0x800u) == 0x10cu,
          "STM pc stored %08x expect 0000010c", m_r32(NULL, 0x800u));
}

static void test_branch(void) {
    /* B +8 (skip one), then MOV r0,#1 (skipped), then MOV r0,#7 */
    /* At pc=0: B to pc=0+8+off. We want to land at word index 2 (addr 8).
       target = pc+8+off = 0+8+off = 8 => off=0 -> encoding e a 000000 */
    uint32_t p[] = { 0xea000000, /* B #8 -> lands at 0x8 */
                     0xe3a00001, /* MOV r0,#1 (skipped) */
                     0xe3a00007  /* MOV r0,#7 */ };
    arm_cpu_t c; load_and_run(&c, p, 3, 2);
    CHECK(c.r[0] == 7, "r0=%u expect 7 (branch skipped the #1)", c.r[0]);
}

static void test_bl_sets_lr(void) {
    /* BL to somewhere; check LR = pc+4 */
    uint32_t p[] = { 0xeb000002 /* BL #... */ };
    arm_cpu_t c; load_and_run(&c, p, 1, 1);
    CHECK(c.r[14] == 4, "lr=%u expect 4", c.r[14]);
}

static void test_ldr_str(void) {
    /* MOV r0,#0xAB ; MOV r1,#0x400 ; STR r0,[r1] ; LDR r2,[r1] */
    uint32_t p[] = { 0xe3a000ab, 0xe3a01b01 /*MOV r1,#0x400*/,
                     0xe5810000 /*STR r0,[r1]*/, 0xe5912000 /*LDR r2,[r1]*/ };
    arm_cpu_t c; load_and_run(&c, p, 4, 4);
    CHECK(c.r[2] == 0xab, "r2=%08x expect ab", c.r[2]);
    CHECK(m_r32(NULL, 0x400) == 0xab, "mem[0x400]=%08x expect ab", m_r32(NULL,0x400));
}

static void test_ldrb(void) {
    /* Load the word 0x11223344 from a PC-relative literal, store it at 0x500,
     * then LDRB from 0x500 -> 0x44, proving little-endian byte extraction. */
    uint32_t p[] = { 0xe3a01c05 /*MOV  r1,#0x500        addr 0 */,
                     0xe59f0004 /*LDR  r0,[pc,#4]       addr 4 (pc+8=0xC, +4=0x10) */,
                     0xe5810000 /*STR  r0,[r1]          addr 8 */,
                     0xe5d12000 /*LDRB r2,[r1]          addr 0xC */,
                     0x11223344 /*literal               addr 0x10 */ };
    arm_cpu_t c; load_and_run(&c, p, 5, 4);
    CHECK(c.r[0] == 0x11223344u, "r0=%08x expect 11223344", c.r[0]);
    CHECK(c.r[2] == 0x44, "r2=%02x expect 44 (LE low byte)", c.r[2]);
}

static void test_cond_not_taken(void) {
    /* MOVEQ r0,#9 with Z clear -> not executed; r0 stays 0 */
    uint32_t p[] = { 0x03a00009 /*MOVEQ r0,#9*/ };
    arm_cpu_t c; load_and_run(&c, p, 1, 1);
    CHECK(c.r[0] == 0, "r0=%u expect 0 (cond failed)", c.r[0]);
}

static void test_mul(void) {
    /* MOV r1,#6 ; MOV r2,#7 ; MUL r0,r1,r2 -> 42  (MUL rd,rm,rs: e0000291) */
    uint32_t p[] = { 0xe3a01006, 0xe3a02007, 0xe0000291 };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.r[0] == 42, "r0=%u expect 42", c.r[0]);
}

/* ARMv5TE's halfword multiply encodings are easy to transpose: x (bit 5)
 * selects Rm while y (bit 6) selects Rs.  Use four different signed halves so
 * every selector combination has a distinct answer. */
static void test_dsp_smul_halfword_selectors_and_real_alias(void) {
    static const struct {
        uint32_t insn, expect;
        const char *name;
    } cases[] = {
        { 0xe1600281u, 0xfffffff1u, "SMULBB" }, /*  3 * -5 = -15 */
        { 0xe16002a1u, 0x0000000au, "SMULTB" }, /* -2 * -5 =  10 */
        { 0xe16002c1u, 0x0000000cu, "SMULBT" }, /*  3 *  4 =  12 */
        { 0xe16002e1u, 0xfffffff8u, "SMULTT" }, /* -2 *  4 =  -8 */
    };

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        arm_cpu_t c;
        load_and_run(&c, &cases[i].insn, 1, 0);
        c.r[1] = 0xfffe0003u;
        c.r[2] = 0x0004fffbu;
        c.cpsr |= ARM_CPSR_N | ARM_CPSR_C | ARM_CPSR_Q;
        uint32_t flags = c.cpsr;
        arm_status_t st = arm_step(&c);
        CHECK(st == ARM_OK && c.r[0] == cases[i].expect,
              "%s status=%d result=%08x expect %08x", cases[i].name,
              (int)st, c.r[0], cases[i].expect);
        CHECK(c.cpsr == flags, "%s changed CPSR %08x -> %08x",
              cases[i].name, flags, c.cpsr);
    }

    /* Exact instruction and live register values at the current real-guest
     * stop.  Rd aliases Rs, so all sources must be read before writeback. */
    const uint32_t blocker = 0xe1630381u;       /* SMULBB r3,r1,r3 */
    arm_cpu_t c;
    load_and_run(&c, &blocker, 1, 0);
    c.r[1] = 1u;
    c.r[3] = 0x34u;
    CHECK(arm_step(&c) == ARM_OK && c.r[3] == 0x34u && c.r[15] == 4u,
          "real SMULBB alias result=%08x pc=%08x", c.r[3], c.r[15]);
}

static void test_dsp_smla_wraps_and_sets_sticky_q(void) {
    const uint32_t smlabb = 0xe1003281u;        /* SMLABB r0,r1,r2,r3 */
    arm_cpu_t c;

    load_and_run(&c, &smlabb, 1, 0);
    c.r[1] = 1u; c.r[2] = 1u; c.r[3] = 0x7fffffffu;
    c.cpsr |= ARM_CPSR_N | ARM_CPSR_C;
    uint32_t nzcv = c.cpsr & (ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C | ARM_CPSR_V);
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0x80000000u,
          "positive-overflow SMLABB result=%08x", c.r[0]);
    CHECK((c.cpsr & ARM_CPSR_Q) != 0u &&
          (c.cpsr & (ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C | ARM_CPSR_V)) == nzcv,
          "positive-overflow SMLABB flags=%08x", c.cpsr);

    load_and_run(&c, &smlabb, 1, 0);
    c.r[1] = 0xffffu; c.r[2] = 1u; c.r[3] = 0x80000000u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0x7fffffffu &&
          (c.cpsr & ARM_CPSR_Q) != 0u,
          "negative-overflow SMLABB result=%08x flags=%08x", c.r[0], c.cpsr);

    /* Q is sticky, and an accumulator may alias the destination. */
    const uint32_t alias = 0xe1033281u;         /* SMLABB r3,r1,r2,r3 */
    load_and_run(&c, &alias, 1, 0);
    c.r[1] = 2u; c.r[2] = 3u; c.r[3] = 4u;
    c.cpsr |= ARM_CPSR_Q | ARM_CPSR_Z | ARM_CPSR_V;
    uint32_t before = c.cpsr;
    CHECK(arm_step(&c) == ARM_OK && c.r[3] == 10u,
          "aliased SMLABB result=%08x", c.r[3]);
    CHECK(c.cpsr == before, "non-overflow SMLABB changed sticky flags %08x -> %08x",
          before, c.cpsr);
}

static void test_dsp_smlal_sign_carry_wrap_and_alias(void) {
    const uint32_t smlalbb = 0xe1410382u;       /* SMLALBB r0,r1,r2,r3 */
    static const struct {
        uint32_t lo, hi, rm, rs, expect_lo, expect_hi;
        const char *name;
    } cases[] = {
        { 0xffffffffu, 0u, 1u, 1u, 0u, 1u, "low-word carry" },
        { 0u, 0u, 0xffffu, 1u, 0xffffffffu, 0xffffffffu, "negative sign extension" },
        { 0xffffffffu, 0xffffffffu, 1u, 1u, 0u, 0u, "modulo-2^64 wrap" },
    };

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        arm_cpu_t c;
        load_and_run(&c, &smlalbb, 1, 0);
        c.r[0] = cases[i].lo; c.r[1] = cases[i].hi;
        c.r[2] = cases[i].rm; c.r[3] = cases[i].rs;
        c.cpsr |= ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C | ARM_CPSR_V | ARM_CPSR_Q;
        uint32_t flags = c.cpsr;
        arm_status_t st = arm_step(&c);
        CHECK(st == ARM_OK && c.r[0] == cases[i].expect_lo &&
              c.r[1] == cases[i].expect_hi,
              "SMLALBB %s status=%d got=%08x:%08x expect=%08x:%08x",
              cases[i].name, (int)st, c.r[1], c.r[0],
              cases[i].expect_hi, cases[i].expect_lo);
        CHECK(c.cpsr == flags, "SMLALBB %s changed flags", cases[i].name);
    }

    /* Rm aliases RdLo. The old low accumulator value is both an input to the
     * multiplication and part of the 64-bit add. */
    const uint32_t alias = 0xe1410280u;         /* SMLALBB r0,r1,r0,r2 */
    arm_cpu_t c;
    load_and_run(&c, &alias, 1, 0);
    c.r[0] = 2u; c.r[1] = 0u; c.r[2] = 3u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 8u && c.r[1] == 0u,
          "aliased SMLALBB got=%08x:%08x", c.r[1], c.r[0]);
}

static void test_dsp_word_halfword_extract_and_accumulate(void) {
    const uint32_t smulwb = 0xe12002a1u;        /* SMULWB r0,r1,r2 */
    const uint32_t smulwt = 0xe12002e1u;        /* SMULWT r0,r1,r2 */
    arm_cpu_t c;

    /* Bits[47:16] of -1 are all ones. C division by 65536 would incorrectly
     * truncate toward zero here, so this pins the required arithmetic extract. */
    load_and_run(&c, &smulwb, 1, 0);
    c.r[1] = 0xffffffffu; c.r[2] = 1u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0xffffffffu,
          "SMULWB(-1,1)=%08x expect ffffffff", c.r[0]);

    load_and_run(&c, &smulwb, 1, 0);
    c.r[1] = 0x80000000u; c.r[2] = 0x8000u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0x40000000u,
          "SMULWB(INT_MIN,-32768)=%08x expect 40000000", c.r[0]);

    load_and_run(&c, &smulwt, 1, 0);
    c.r[1] = 0x00010000u; c.r[2] = 0xfffe0001u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0xfffffffeu,
          "SMULWT top-half result=%08x expect fffffffe", c.r[0]);

    const uint32_t smlawb = 0xe1203281u;        /* SMLAWB r0,r1,r2,r3 */
    load_and_run(&c, &smlawb, 1, 0);
    c.r[1] = 0x00010000u; c.r[2] = 1u; c.r[3] = 0x7fffffffu;
    c.cpsr |= ARM_CPSR_N | ARM_CPSR_C;
    uint32_t nzcv = c.cpsr & (ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C | ARM_CPSR_V);
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0x80000000u &&
          (c.cpsr & ARM_CPSR_Q) != 0u,
          "overflowing SMLAWB result=%08x flags=%08x", c.r[0], c.cpsr);
    CHECK((c.cpsr & (ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C | ARM_CPSR_V)) == nzcv,
          "SMLAWB changed NZCV flags=%08x", c.cpsr);

    const uint32_t smlawt = 0xe12032c1u;        /* SMLAWT r0,r1,r2,r3 */
    load_and_run(&c, &smlawt, 1, 0);
    c.r[1] = 0x00010000u; c.r[2] = 0x00010000u; c.r[3] = 5u;
    c.cpsr |= ARM_CPSR_Q | ARM_CPSR_Z | ARM_CPSR_V;
    uint32_t flags = c.cpsr;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 6u && c.cpsr == flags,
          "non-overflow SMLAWT result=%08x flags=%08x", c.r[0], c.cpsr);
}

static void test_dsp_multiply_unpredictable_and_reserved_forms_trap(void) {
    /* Every named R15 operand, SMLAL's equal destination pair, and reserved
     * near-misses must stop without corrupting registers or flags. */
#define SMLA(rd,rn,rs,rm)  (0xe1000080u | ((rd)<<16) | ((rn)<<12) | ((rs)<<8) | (rm))
#define SMLAL(hi,lo,rs,rm) (0xe1400080u | ((hi)<<16) | ((lo)<<12) | ((rs)<<8) | (rm))
#define SMLAW(rd,rn,rs,rm) (0xe1200080u | ((rd)<<16) | ((rn)<<12) | ((rs)<<8) | (rm))
#define SMUL(rd,rs,rm)     (0xe1600080u | ((rd)<<16) | ((rs)<<8) | (rm))
#define SMULW(rd,rs,rm)    (0xe12000a0u | ((rd)<<16) | ((rs)<<8) | (rm))
    static const uint32_t bad[] = {
        SMLA(15u,4u,2u,1u), SMLA(3u,15u,2u,1u),
        SMLA(3u,4u,15u,1u), SMLA(3u,4u,2u,15u),
        SMLAL(15u,4u,2u,1u), SMLAL(3u,15u,2u,1u),
        SMLAL(3u,4u,15u,1u), SMLAL(3u,4u,2u,15u),
        SMLAL(3u,3u,2u,1u),
        SMLAW(15u,4u,2u,1u), SMLAW(3u,15u,2u,1u),
        SMLAW(3u,4u,15u,1u), SMLAW(3u,4u,2u,15u),
        SMUL(15u,2u,1u), SMUL(3u,15u,1u), SMUL(3u,2u,15u),
        SMULW(15u,2u,1u), SMULW(3u,15u,1u), SMULW(3u,2u,15u),
        0xe1601281u,                         /* SMULxy with SBZ[15:12] != 0 */
        0xe12012a1u,                         /* SMULWy with SBZ[15:12] != 0 */
        0xe1600291u,                         /* reserved bit 4 set */
        0xe1600201u,                         /* required bit 7 clear */
        0xf1600281u,                         /* cond=1111 is not this instruction */
    };
#undef SMLA
#undef SMLAL
#undef SMLAW
#undef SMUL
#undef SMULW

    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        arm_cpu_t c;
        load_and_run(&c, &bad[i], 1, 0);
        for (unsigned r = 0; r < 15u; r++) c.r[r] = 0x11110000u + r;
        c.cpsr |= ARM_CPSR_N | ARM_CPSR_C | ARM_CPSR_Q;
        uint32_t regs[15]; memcpy(regs, c.r, sizeof regs);
        uint32_t cpsr = c.cpsr;
        arm_status_t st = arm_step(&c);
        CHECK(st == ARM_UNDEFINED, "bad DSP encoding %08x status=%d", bad[i], (int)st);
        CHECK(memcmp(regs, c.r, sizeof regs) == 0 && c.r[15] == 0u && c.cpsr == cpsr,
              "bad DSP encoding %08x changed architectural state", bad[i]);
    }

    /* A legal instruction with a failed condition is a true NOP. */
    const uint32_t eq = 0x01630381u;          /* SMULBBEQ r3,r1,r3 */
    arm_cpu_t c;
    load_and_run(&c, &eq, 1, 0);
    c.r[1] = 7u; c.r[3] = 9u;                /* Z is clear after reset */
    uint32_t cpsr = c.cpsr;
    CHECK(arm_step(&c) == ARM_OK && c.r[3] == 9u && c.r[15] == 4u &&
          c.cpsr == cpsr,
          "condition-failed DSP instruction was not a NOP");
}

static void test_orr_bic_mvn(void) {
    /* MOV r0,#0xF0 ; ORR r0,r0,#0x0F -> 0xFF ; BIC r0,r0,#0x0F -> 0xF0 ; MVN r1,#0 -> 0xffffffff */
    uint32_t p[] = { 0xe3a000f0, 0xe380000f, 0xe3c0000f, 0xe3e01000 };
    arm_cpu_t c; load_and_run(&c, p, 4, 4);
    CHECK(c.r[0] == 0xf0, "r0=%08x expect f0", c.r[0]);
    CHECK(c.r[1] == 0xffffffffu, "r1=%08x expect ffffffff", c.r[1]);
}

static void test_bx_branches(void) {
    /* MOV r1,#0x100 ; BX r1  -> PC = 0x100 (regression: BX must actually branch,
     * not silently execute as a TEQ comparison). */
    uint32_t p[] = { 0xe3a01c01 /*MOV r1,#0x100*/, 0xe12fff11 /*BX r1*/ };
    arm_cpu_t c; load_and_run(&c, p, 2, 2);
    CHECK(c.r[15] == 0x100, "pc=%08x expect 100 (BX branched)", c.r[15]);
}

/*
 * Returning from an exception into Thumb code must land on the halfword the
 * SPSR says, not on the word below it.
 *
 * This one cost a real boot. The kernel's decrementer FIQ interrupts Thumb
 * code roughly half the time at an address that is 2 mod 4. Masking the
 * resume address with ~3 rewinds it by two bytes, so the guest re-executes
 * the *preceding* halfword. In xnu-1357 that turned a zone free into a jump
 * onto the locked path's tail, which then unlocked a mutex whose pointer was
 * a stale scratch value of 1 -- surfacing as a data abort at
 * _lck_mtx_unlock+0x8 with DFAR 0x1, a mile from the actual bug.
 *
 * The tell was statistical: every single one of 761 exception returns landed
 * 4-byte aligned, where hardware would be about half and half.
 */
static void test_exception_return_to_thumb_keeps_halfword(void) {
    uint32_t p[] = { 0xe1b0f00e };   /* MOVS pc, lr */
    arm_cpu_t c; load_and_run(&c, p, 1, 0);   /* load only; we set up state */

    arm_set_mode(&c, ARM_MODE_FIQ);
    c.spsr[arm_bank_of_mode(ARM_MODE_FIQ)] = ARM_MODE_SVC | ARM_CPSR_T;
    c.r[14] = 0x0000101a;            /* Thumb, and 2 mod 4 */
    c.r[15] = 0;
    arm_step(&c);

    CHECK(c.r[15] == 0x0000101a,
          "pc=%08x expect 101a (a ~3 mask would rewind it to 1018)", c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_T) != 0, "should have resumed in Thumb state");
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC,
          "mode=%02x expect SVC restored from SPSR",
          c.cpsr & ARM_CPSR_MODE_MASK);
}

/* The same return into ARM code must still be word-aligned. */
static void test_exception_return_to_arm_stays_word_aligned(void) {
    uint32_t p[] = { 0xe1b0f00e };   /* MOVS pc, lr */
    arm_cpu_t c; load_and_run(&c, p, 1, 0);

    arm_set_mode(&c, ARM_MODE_FIQ);
    c.spsr[arm_bank_of_mode(ARM_MODE_FIQ)] = ARM_MODE_SVC;  /* T clear */
    c.r[14] = 0x0000101a;
    c.r[15] = 0;
    arm_step(&c);

    CHECK(c.r[15] == 0x00001018, "pc=%08x expect 1018 (ARM state aligns to 4)",
          c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_T) == 0, "should have resumed in ARM state");
}

/*
 * LDM {..., pc}^ is an exception return, and differs from a plain POP {..,pc}
 * in two ways that are easy to get wrong together: bit 0 of the loaded word is
 * part of the address rather than a Thumb selector, and the alignment must
 * follow the T bit the SPSR restores. Doing the PC write before the restore
 * gets both wrong.
 */
static void test_ldm_exception_return_takes_state_from_spsr(void) {
    /* LDMIA sp, {pc}^  ->  0xe8dd8000 */
    uint32_t p[] = { 0xe8dd8000u };
    arm_cpu_t c; load_and_run(&c, p, 1, 0);

    arm_set_mode(&c, ARM_MODE_IRQ);
    c.spsr[arm_bank_of_mode(ARM_MODE_IRQ)] = ARM_MODE_SVC | ARM_CPSR_T;
    c.r[13] = 0x800;
    c.bus->write32(c.bus->ctx, 0x800, 0x0000101au);  /* even: no Thumb bit set */
    c.r[15] = 0;
    arm_step(&c);

    CHECK(c.r[15] == 0x0000101a, "pc=%08x expect 101a (halfword, from SPSR.T)",
          c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_T) != 0,
          "T must come from the SPSR, not from bit 0 of the loaded word");
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC,
          "mode=%02x expect SVC restored from SPSR",
          c.cpsr & ARM_CPSR_MODE_MASK);

    /* In ARM state bit 1 cannot be represented. LDM(3) must reject the frame
     * after reading it but before committing PC, writeback, CPSR, or any other
     * loaded register. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe8fd8001u);               /* LDMIA sp!,{r0,pc}^ */
    m_w32(NULL, 0x800u, 0xfeedfaceu);
    m_w32(NULL, 0x804u, 0x0000101au);           /* invalid ARM halfword */
    arm_reset(&c, &g_bus);
    arm_set_mode(&c, ARM_MODE_IRQ);
    c.spsr[ARM_BANK_IRQ] = ARM_MODE_SVC;         /* T clear */
    c.r[0] = 0x11111111u;
    c.r[13] = 0x800u;
    c.r[15] = 0u;
    uint32_t before_cpsr = c.cpsr;
    CHECK(arm_step(&c) == ARM_UNDEFINED,
          "LDM(3) accepted an ARM-state target with bit 1 set");
    CHECK(c.r[0] == 0x11111111u && c.r[13] == 0x800u && c.r[15] == 0u &&
          c.cpsr == before_cpsr,
          "invalid LDM(3) target committed registers/writeback/CPSR");
}

/* RFE restores PC and CPSR from memory; the same alignment rule applies. */
static void test_rfe_aligns_for_the_restored_state(void) {
    /* RFEIA r0 -> 0xf8900a00 (P=0, U=1: read from [r0], not [r0-4]) */
    uint32_t p[] = { 0xf8900a00u };
    arm_cpu_t c; load_and_run(&c, p, 1, 0);

    arm_set_mode(&c, ARM_MODE_IRQ);
    c.r[0] = 0x900;
    c.bus->write32(c.bus->ctx, 0x900, 0x00001018u);      /* aligned ARM pc */
    c.bus->write32(c.bus->ctx, 0x904, ARM_MODE_SVC);     /* new cpsr, T clear */
    c.r[15] = 0;
    arm_step(&c);

    CHECK(c.r[15] == 0x00001018,
          "pc=%08x expect 1018 (ARM state must align to a word)", c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_T) == 0, "should have resumed in ARM state");

    /* The same frame with bit 1 set is UNPREDICTABLE, not rounded down. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xf8b00a00u);                       /* RFEIA r0! */
    m_w32(NULL, 0x900u, 0x0000101au);
    m_w32(NULL, 0x904u, ARM_MODE_SVC);
    arm_reset(&c, &g_bus);
    arm_set_mode(&c, ARM_MODE_IRQ);
    c.r[0] = 0x900u;
    c.r[15] = 0u;
    uint32_t before_cpsr = c.cpsr;
    CHECK(arm_step(&c) == ARM_UNDEFINED,
          "RFE accepted an ARM-state target with bit 1 set");
    CHECK(c.r[0] == 0x900u && c.r[15] == 0u && c.cpsr == before_cpsr,
          "invalid RFE target committed base/PC/CPSR");
}

static void test_srs_and_rfe_reject_unprivileged_execution(void) {
    arm_cpu_t c;

    /* A User-mode SRS must be rejected before touching the target mode's stack. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xf8cd0513u);                  /* SRSIA sp,#SVC */
    arm_reset(&c, &g_bus);
    arm_set_mode(&c, ARM_MODE_USR);
    c.bank_r13[ARM_BANK_SVC] = 0x900u;
    c.r[14] = 0x11223344u;
    c.r[15] = 0;
    g_watch_addr = 0x900u; g_watch_reads32 = g_watch_writes32 = 0;
    CHECK(arm_step(&c) == ARM_UNDEFINED, "User-mode SRS must be undefined");
    CHECK(g_watch_writes32 == 0u, "User-mode SRS performed %u target-stack writes",
          g_watch_writes32);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR,
          "User-mode SRS changed mode to %02x", c.cpsr & ARM_CPSR_MODE_MASK);

    /* RFE can replace every CPSR mode bit, so privilege must be checked before
     * even reading its attacker-controlled frame (which may be MMIO). */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xf8900a00u);                  /* RFEIA r0 */
    m_w32(NULL, 0x900u, 0x1000u);
    m_w32(NULL, 0x904u, ARM_MODE_SVC);
    arm_reset(&c, &g_bus);
    arm_set_mode(&c, ARM_MODE_USR);
    c.r[0] = 0x900u;
    c.r[15] = 0;
    g_watch_addr = 0x900u; g_watch_reads32 = g_watch_writes32 = 0;
    CHECK(arm_step(&c) == ARM_UNDEFINED, "User-mode RFE must be undefined");
    CHECK(g_watch_reads32 == 0u, "User-mode RFE performed %u frame reads",
          g_watch_reads32);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR,
          "User-mode RFE escalated to mode %02x", c.cpsr & ARM_CPSR_MODE_MASK);

    /* System mode is privileged but has no SPSR; fabricating one from CPSR made
     * SRS silently wrong. Refuse that architecturally unpredictable form. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xf8cd0513u);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.bank_r13[ARM_BANK_SVC] = 0x900u;
    c.r[15] = 0;
    g_watch_addr = 0x900u; g_watch_reads32 = g_watch_writes32 = 0;
    CHECK(arm_step(&c) == ARM_UNDEFINED, "System-mode SRS must be refused");
    CHECK(g_watch_writes32 == 0u, "System-mode SRS performed %u target-stack writes",
          g_watch_writes32);

    g_watch_addr = 0xffffffffu;
}

static void test_exception_returns_reject_invalid_modes_before_mutation(void) {
    arm_cpu_t c;

    /* An invalid current mode must not alias to the User bank and then execute
     * with the privilege checks accidentally passing. Reject it before fetch. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe1a00000u);                 /* NOP */
    arm_reset(&c, &g_bus);
    c.cpsr &= ~ARM_CPSR_MODE_MASK;                /* unimplemented mode 0 */
    c.r[15] = 0u;
    g_watch_addr = 0u; g_watch_reads32 = g_watch_writes32 = 0u;
    CHECK(arm_step(&c) == ARM_UNDEFINED, "invalid current CPSR mode must trap");
    CHECK(g_watch_reads32 == 0u, "invalid current mode fetched %u instructions",
          g_watch_reads32);

    /* R15 is not a legal RFE base. Refuse it before reading the PC-relative
     * address that the generic register helper would otherwise synthesize. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xf89f0a00u);                 /* RFEIA pc (UNPREDICTABLE) */
    arm_reset(&c, &g_bus);
    c.r[15] = 0u;
    g_watch_addr = 8u; g_watch_reads32 = g_watch_writes32 = 0u;
    CHECK(arm_step(&c) == ARM_UNDEFINED, "RFE with Rn=pc must be refused");
    CHECK(g_watch_reads32 == 0u, "RFE pc issued %u frame reads", g_watch_reads32);

    /* A malformed frame must not rebank through arm_bank_of_mode's historical
     * default-to-User fallback or apply writeback before it is validated. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xf8b00a00u);                 /* RFEIA r0! */
    m_w32(NULL, 0x900u, 0x1000u);
    m_w32(NULL, 0x904u, 0u);                      /* invalid restored mode */
    arm_reset(&c, &g_bus);
    c.r[0] = 0x900u;
    c.r[15] = 0u;
    uint32_t before = c.cpsr;
    CHECK(arm_step(&c) == ARM_UNDEFINED, "RFE accepted an invalid CPSR mode");
    CHECK(c.cpsr == before && c.r[15] == 0u && c.r[0] == 0x900u,
          "invalid RFE mutated cpsr/pc/base: %08x/%08x/%08x",
          c.cpsr, c.r[15], c.r[0]);

    /* SRS's target mode field is also guest-controlled. Invalid modes must be
     * rejected before selecting a bank or exposing exception state to memory. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xf8cd0500u);                 /* SRSIA sp,#invalid-0 */
    arm_reset(&c, &g_bus);
    c.bank_r13[ARM_BANK_USR] = 0x900u;
    c.r[15] = 0u;
    g_watch_addr = 0x900u; g_watch_reads32 = g_watch_writes32 = 0u;
    CHECK(arm_step(&c) == ARM_UNDEFINED, "SRS accepted an invalid target mode");
    CHECK(g_watch_writes32 == 0u, "invalid SRS issued %u state writes",
          g_watch_writes32);

    /* LDM^ and data-processing exception returns both source the mode from an
     * SPSR. They must validate it before memory or arithmetic has side effects. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe8d08000u);                 /* LDMIA r0,{pc}^ */
    arm_reset(&c, &g_bus);
    c.spsr[ARM_BANK_SVC] = 0u;
    c.r[0] = 0x900u;
    c.r[15] = 0u;
    g_watch_addr = 0x900u; g_watch_reads32 = g_watch_writes32 = 0u;
    CHECK(arm_step(&c) == ARM_UNDEFINED, "LDM^ accepted an invalid SPSR mode");
    CHECK(g_watch_reads32 == 0u, "invalid LDM^ issued %u frame reads",
          g_watch_reads32);

    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe25ef004u);                 /* SUBS pc,lr,#4 */
    arm_reset(&c, &g_bus);
    c.spsr[ARM_BANK_SVC] = 0u;
    c.r[14] = 0x1000u;
    c.r[15] = 0u;
    before = c.cpsr;
    CHECK(arm_step(&c) == ARM_UNDEFINED,
          "data-processing exception return accepted invalid SPSR mode");
    CHECK(c.cpsr == before && c.r[15] == 0u,
          "invalid SUBS pc mutated cpsr/pc: %08x/%08x", c.cpsr, c.r[15]);
    g_watch_addr = 0xffffffffu;
}

/*
 * SETEND LE is a no-op for a little-endian machine and must execute. SETEND BE
 * must keep trapping: we do not model a big-endian data path, so honouring it
 * would silently corrupt every subsequent load.
 */
static void test_setend_le_runs_be_traps(void) {
    uint32_t le[] = { 0xf1010000u, 0xe3a0002au };   /* SETEND LE ; MOV r0,#42 */
    arm_cpu_t c; load_and_run(&c, le, 2, 2);
    CHECK(c.r[0] == 42, "r0=%u expect 42 (SETEND LE must be a no-op)", c.r[0]);

    uint32_t be[] = { 0xf1010200u };                /* SETEND BE */
    arm_cpu_t d; arm_status_t st = run_status(&d, be, 1, 1);
    CHECK(st == ARM_UNDEFINED,
          "status=%d expect ARM_UNDEFINED for SETEND BE", (int)st);
}

/*
 * The 64-bit multiplies. A driver in the real 3.1.3 kernelcache stopped the
 * boot on a plain UMULL, so these are ordinary compiled code rather than an
 * exotic corner. The signed/unsigned distinction is the part worth pinning:
 * doing both in 64-bit unsigned yields the correct low word and a silently
 * wrong high word, which is the kind of error that surfaces a long way away.
 */
static void test_umull_and_smull(void) {
    /* UMULL r0,r1,r2,r3 with 0xFFFFFFFF * 0xFFFFFFFF = 0xFFFFFFFE00000001 */
    uint32_t p[] = { 0xe3e02000 /* MVN r2,#0  -> 0xffffffff */,
                     0xe3e03000 /* MVN r3,#0  -> 0xffffffff */,
                     0xe0810392 /* UMULL r0,r1,r2,r3        */ };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.r[0] == 0x00000001u, "lo=%08x expect 00000001", c.r[0]);
    CHECK(c.r[1] == 0xfffffffeu, "hi=%08x expect fffffffe", c.r[1]);

    /* SMULL of the same bit patterns is (-1) * (-1) = 1, so the high word is
     * ZERO. Same inputs, different answer — this is the check that catches an
     * unsigned implementation of the signed form. */
    uint32_t q[] = { 0xe3e02000, 0xe3e03000,
                     0xe0c10392 /* SMULL r0,r1,r2,r3 */ };
    arm_cpu_t d; load_and_run(&d, q, 3, 3);
    CHECK(d.r[0] == 0x00000001u, "lo=%08x expect 00000001 (-1 * -1)", d.r[0]);
    CHECK(d.r[1] == 0x00000000u,
          "hi=%08x expect 00000000 — a signed multiply done unsigned gives "
          "fffffffe here", d.r[1]);
}

static void test_umlal_accumulates(void) {
    /* r0:r1 = 5, then UMLAL r0,r1,r2,r3 with 3 * 7 -> 26 */
    uint32_t p[] = { 0xe3a00005 /* MOV r0,#5 */,
                     0xe3a01000 /* MOV r1,#0 */,
                     0xe3a02003 /* MOV r2,#3 */,
                     0xe3a03007 /* MOV r3,#7 */,
                     0xe0a10392 /* UMLAL r0,r1,r2,r3 */ };
    arm_cpu_t c; load_and_run(&c, p, 5, 5);
    CHECK(c.r[0] == 26, "lo=%u expect 26 (5 + 3*7)", c.r[0]);
    CHECK(c.r[1] == 0,  "hi=%u expect 0", c.r[1]);
}

static void test_clz(void) {
    /* CLZ r0,r1 -> e16f0f11.  Compilers emit this constantly. */
    uint32_t p[] = { 0xe3a01001 /* MOV r1,#1        */, 0xe16f0f11 /* CLZ r0,r1 */,
                     0xe3a01000 /* MOV r1,#0        */, 0xe16f2f11 /* CLZ r2,r1 */,
                     0xe3e01000 /* MVN r1,#0 -> ~0  */, 0xe16f3f11 /* CLZ r3,r1 */ };
    arm_cpu_t c; load_and_run(&c, p, 6, 6);
    CHECK(c.r[0] == 31, "clz(1)=%u expect 31", c.r[0]);
    CHECK(c.r[2] == 32, "clz(0)=%u expect 32 — the case that tempts a loop bug", c.r[2]);
    CHECK(c.r[3] == 0,  "clz(0xffffffff)=%u expect 0", c.r[3]);
}

/*
 * Saturating arithmetic clamps instead of wrapping, and sets the sticky Q
 * flag. Wrapping would be silently correct everywhere except the extremes,
 * which is exactly where these instructions are used.
 */
static void test_qadd_saturates_and_sets_q(void) {
    /* r1 = 0x7fffffff, r2 = 1, QADD r0,r2,r1  (Rd, Rm, Rn) */
    uint32_t p[] = { 0xe3e01102 /* MVN r1,#0x80000000 -> 0x7fffffff */,
                     0xe3a02001 /* MOV r2,#1                        */,
                     0xe1010052 /* QADD r0,r2,r1                    */ };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.r[0] == 0x7fffffffu,
          "r0=%08x expect 7fffffff (clamped, not wrapped to 80000000)", c.r[0]);
    CHECK((c.cpsr & ARM_CPSR_Q) != 0, "Q must be set on saturation");

    /* Without saturation Q must stay clear and the result is ordinary. */
    uint32_t q[] = { 0xe3a01005 /* MOV r1,#5 */, 0xe3a02003 /* MOV r2,#3 */,
                     0xe1010052 /* QADD r0,r2,r1 */ };
    arm_cpu_t d; load_and_run(&d, q, 3, 3);
    CHECK(d.r[0] == 8, "r0=%u expect 8", d.r[0]);
    CHECK((d.cpsr & ARM_CPSR_Q) == 0, "Q must not be set without saturation");
}

static void test_swp_exchanges(void) {
    /* SWP is an atomic read-then-write: Rd gets the old memory word and the
     * new value comes from Rm. Getting the order wrong (writing before
     * reading) silently returns the value just stored, which looks correct
     * whenever Rd and Rm happen to be the same register. */
    uint32_t p[] = { 0xe3a01b02 /* MOV r1,#0x800   */,
                     0xe3a000aa /* MOV r0,#0xAA    */,
                     0xe5810000 /* STR r0,[r1]     seed memory with 0xAA */,
                     0xe3a004bb /* MOV r0,#0xBB000000 */,
                     0xe1012090 /* SWP r2,r0,[r1]  */,
                     0xe5913000 /* LDR r3,[r1]     */ };
    arm_cpu_t c; load_and_run(&c, p, 6, 6);
    CHECK(c.r[2] == 0xaa, "r2=%08x expect aa (SWP returns the OLD word)", c.r[2]);
    CHECK(c.r[3] == 0xbb000000, "r3=%08x expect bb000000 (SWP stored Rm)", c.r[3]);
}

static void test_swp_legacy_unaligned_word_semantics(void) {
    arm_cpu_t c;

    /* U=0,A=0 defines SWP as WLoad followed by WStore: align down, rotate the
     * loaded word by the byte offset, then store the new word unrotated at the
     * aligned address. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe1012090u);              /* SWP r2,r0,[r1] */
    m_w32(NULL, 0x800u, 0x11223344u);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.cp15.sctlr &= ~(ARM_SCTLR_U | ARM_SCTLR_A);
    c.r[0] = 0xa1b2c3d4u;
    c.r[1] = 0x801u;
    g_watch_addr = 0x800u;
    g_watch_reads32 = g_watch_writes32 = 0u;
    CHECK(arm_step(&c) == ARM_OK && c.r[2] == 0x44112233u,
          "legacy unaligned SWP returned %08x", c.r[2]);
    CHECK(g_watch_reads32 == 1u && g_watch_writes32 == 1u,
          "legacy SWP issued reads=%u writes=%u at aligned address",
          g_watch_reads32, g_watch_writes32);
    g_watch_addr = 0xffffffffu;
    CHECK(m_r32(NULL, 0x800u) == 0xa1b2c3d4u,
          "legacy SWP stored %08x", m_r32(NULL, 0x800u));

    /* Either modern unaligned control makes a misaligned SWP alignment-fault
     * before its read half; the write nature is preserved in DFSR.WnR. */
    for (unsigned mode = 1u; mode <= 3u; mode++) {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0u, 0xe1012090u);
        m_w32(NULL, 0x800u, 0x11223344u);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
        c.cp15.sctlr &= ~(ARM_SCTLR_U | ARM_SCTLR_A);
        if (mode & 1u) c.cp15.sctlr |= ARM_SCTLR_U;
        if (mode & 2u) c.cp15.sctlr |= ARM_SCTLR_A;
        c.r[0] = 0xa1b2c3d4u;
        c.r[1] = 0x801u;
        g_watch_addr = 0x800u;
        g_watch_reads32 = g_watch_writes32 = 0u;
        CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT &&
              (c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT &&
              (c.cp15.dfsr & (1u << 11)) != 0u && c.cp15.dfar == 0x801u,
              "misaligned SWP mode=%u did not alignment-fault", mode);
        CHECK(g_watch_reads32 == 0u && g_watch_writes32 == 0u,
              "faulting SWP mode=%u touched memory", mode);
    }
    g_watch_addr = 0xffffffffu;
}

/*
 * LDREXD/STREXD: the doubleword exclusive pair the kernel's 64-bit atomics
 * use. OSAddAtomic64 is the first one a real boot reaches, so this encoding
 * is load-bearing rather than obscure.
 */
static void test_ldrexd_strexd_roundtrip(void) {
    uint32_t p[] = { 0xe3a01b02 /* MOV r1,#0x800        */,
                     0xe3a00012 /* MOV r0,#0x12         */,
                     0xe5810000 /* STR r0,[r1]          low word  */,
                     0xe3a00034 /* MOV r0,#0x34         */,
                     0xe5810004 /* STR r0,[r1,#4]       high word */,
                     0xe1b14f9f /* LDREXD r4,r5,[r1]    */,
                     0xe3a06056 /* MOV r6,#0x56         */,
                     0xe3a07078 /* MOV r7,#0x78         */,
                     0xe1a13f96 /* STREXD r3,r6,r7,[r1] */,
                     0xe5918000 /* LDR r8,[r1]          */,
                     0xe5919004 /* LDR r9,[r1,#4]       */ };
    arm_cpu_t c; load_and_run(&c, p, 11, 11);
    CHECK(c.r[4] == 0x12, "r4=%08x expect 12 (LDREXD low)",  c.r[4]);
    CHECK(c.r[5] == 0x34, "r5=%08x expect 34 (LDREXD high)", c.r[5]);
    CHECK(c.r[3] == 0,    "r3=%08x expect 0 (STREXD succeeded)", c.r[3]);
    CHECK(c.r[8] == 0x56, "r8=%08x expect 56 (STREXD low)",  c.r[8]);
    CHECK(c.r[9] == 0x78, "r9=%08x expect 78 (STREXD high)", c.r[9]);
}

/*
 * CLREX must actually drop the monitor. If it silently does nothing, a STREX
 * that the architecture requires to fail will succeed, and two threads can
 * both believe they hold the same lock.
 */
static void test_clrex_makes_strex_fail(void) {
    uint32_t p[] = { 0xe3a01b02 /* MOV r1,#0x800   */,
                     0xe1910f9f /* LDREX r0,[r1]   */,
                     0xf57ff01f /* CLREX           */,
                     0xe1812f90 /* STREX r2,r0,[r1]*/ };
    arm_cpu_t c; load_and_run(&c, p, 4, 4);
    CHECK(c.r[2] == 1, "r2=%08x expect 1 (STREX must fail after CLREX)", c.r[2]);
}

static void test_failed_strex_still_checks_write_permission(void) {
    arm_cpu_t c;
    const uint32_t va = 0x80000800u;

    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe1812f90u);              /* STREX r2,r0,[r1] */
    m_w32(NULL, 0x4000u, (3u << 10) | 2u);     /* user-RW code section */
    m_w32(NULL, 0x6000u, (2u << 10) | 2u);     /* user-RO target section */
    m_w32(NULL, 0x800u, 0x11223344u);
    arm_reset(&c, &g_bus);
    arm_set_mode(&c, ARM_MODE_USR);
    c.cp15.ttbr0 = 0x4000u;
    c.cp15.dacr = 1u;                          /* domain 0: Client */
    c.cp15.sctlr |= ARM_SCTLR_M | ARM_SCTLR_XP;
    c.r[0] = 0xa1b2c3d4u;
    c.r[1] = va;
    c.r[2] = 0xfeedfaceu;
    c.r[15] = 0u;
    c.excl_valid = false;
    g_watch_addr = 0x800u;
    g_watch_writes32 = 0u;

    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "failed-monitor STREX did not take its write-permission abort");
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_SECTION_PERMISSION &&
          (c.cp15.dfsr & (1u << 11)) != 0u && c.cp15.dfar == va,
          "failed-monitor STREX dfsr=%x dfar=%08x", c.cp15.dfsr, c.cp15.dfar);
    CHECK(c.r[2] == 0xfeedfaceu && g_watch_writes32 == 0u,
          "faulting STREX wrote status/target before abort");
    g_watch_addr = 0xffffffffu;
}

/*
 * An exception clears the monitor too. Without this, an interrupt landing
 * between LDREX and STREX leaves the tag intact and the preempted thread's
 * STREX still succeeds -- two owners of one spinlock. Modelled here with SWI,
 * which is the exception we can raise from a test program.
 */
static void test_exception_clears_exclusive_monitor(void) {
    /* Laid out so the SWI vector at 0x08 is part of the same image: branch
     * over it, run LDREX / SWI / STREX, and let the handler return to the
     * STREX via MOV pc,lr. */
    uint32_t p[] = { 0xea000002 /* 0x00 B 0x10        skip the vector */,
                     0x00000000 /* 0x04 (unused)      */,
                     0xe1a0f00e /* 0x08 MOV pc,lr     SWI vector        */,
                     0x00000000 /* 0x0c (unused)      */,
                     0xe3a01b02 /* 0x10 MOV r1,#0x800 */,
                     0xe1910f9f /* 0x14 LDREX r0,[r1] */,
                     0xef000000 /* 0x18 SWI #0        */,
                     0xe1812f90 /* 0x1c STREX r2,r0,[r1] */ };
    arm_cpu_t c; load_and_run(&c, p, 8, 6);
    CHECK(c.r[2] == 1, "r2=%08x expect 1 (STREX must fail after an exception)",
          c.r[2]);
}

static void test_strh_ldrh(void) {
    /* MOV r0,#0x1234-ish via halfword: store 0xAB and read it back as a halfword */
    uint32_t p[] = { 0xe3a000ab /*MOV r0,#0xAB*/,
                     0xe3a01b02 /*MOV r1,#0x800*/,
                     0xe1c100b0 /*STRH r0,[r1]*/,
                     0xe1d120b0 /*LDRH r2,[r1]*/ };
    arm_cpu_t c; load_and_run(&c, p, 4, 4);
    CHECK(c.r[2] == 0xab, "r2=%08x expect ab (LDRH)", c.r[2]);
}

static void test_ldrsb_sign_extends(void) {
    /* store byte 0xFF, load it signed -> 0xFFFFFFFF */
    uint32_t p[] = { 0xe3a000ff /*MOV r0,#0xFF*/,
                     0xe3a01b02 /*MOV r1,#0x800*/,
                     0xe5c10000 /*STRB r0,[r1]*/,
                     0xe1d120d0 /*LDRSB r2,[r1]*/ };
    arm_cpu_t c; load_and_run(&c, p, 4, 4);
    CHECK(c.r[2] == 0xffffffffu, "r2=%08x expect ffffffff (LDRSB)", c.r[2]);
}

static void test_ldrsh_sign_extends(void) {
    /* store halfword 0x8000, load it signed -> 0xFFFF8000 */
    uint32_t p[] = { 0xe3a00902 /*MOV r0,#0x8000*/,
                     0xe3a01b02 /*MOV r1,#0x800*/,
                     0xe1c100b0 /*STRH r0,[r1]*/,
                     0xe1d120f0 /*LDRSH r2,[r1]*/ };
    arm_cpu_t c; load_and_run(&c, p, 4, 4);
    CHECK(c.r[2] == 0xffff8000u, "r2=%08x expect ffff8000 (LDRSH)", c.r[2]);
}

static void test_stmia_ldmia(void) {
    /* STMIA r1!,{r0,r2} then LDMIA r1,{r3,r4} round-trips both words. */
    uint32_t p[] = { 0xe3a00011 /*MOV r0,#0x11*/,
                     0xe3a02022 /*MOV r2,#0x22*/,
                     0xe3a01b02 /*MOV r1,#0x800*/,
                     0xe8a10005 /*STMIA r1!,{r0,r2}*/,
                     0xe3a01b02 /*MOV r1,#0x800*/,
                     0xe8910018 /*LDMIA r1,{r3,r4}*/ };
    arm_cpu_t c; load_and_run(&c, p, 6, 6);
    CHECK(c.r[3] == 0x11, "r3=%08x expect 11", c.r[3]);
    CHECK(c.r[4] == 0x22, "r4=%08x expect 22", c.r[4]);
}

static void test_push_pop(void) {
    /* The classic prologue/epilogue pair: STMDB sp!,{r0,r1} / LDMIA sp!,{r2,r3}.
     * Also checks the stack pointer returns to where it started. */
    uint32_t p[] = { 0xe3a0dc09 /*MOV sp,#0x900*/,
                     0xe3a000aa /*MOV r0,#0xAA*/,
                     0xe3a010bb /*MOV r1,#0xBB*/,
                     0xe92d0003 /*STMDB sp!,{r0,r1}  (push)*/,
                     0xe8bd000c /*LDMIA sp!,{r2,r3}  (pop)*/ };
    arm_cpu_t c; load_and_run(&c, p, 5, 5);
    CHECK(c.r[2] == 0xaa, "r2=%08x expect aa", c.r[2]);
    CHECK(c.r[3] == 0xbb, "r3=%08x expect bb", c.r[3]);
    CHECK(c.r[13] == 0x900, "sp=%08x expect 900 (balanced)", c.r[13]);
}

static void test_ldm_to_pc_branches(void) {
    /* LDMIA sp!,{pc} must branch. Push 0x300 then pop it into PC. */
    uint32_t p[] = { 0xe3a0dc09 /*MOV sp,#0x900*/,
                     0xe3a00c03 /*MOV r0,#0x300*/,
                     0xe92d0001 /*STMDB sp!,{r0}*/,
                     0xe8bd8000 /*LDMIA sp!,{pc}*/ };
    arm_cpu_t c; load_and_run(&c, p, 4, 4);
    CHECK(c.r[15] == 0x300, "pc=%08x expect 300 (LDM to PC branched)", c.r[15]);
}

static void test_plain_loads_to_pc_reject_arm_halfword_targets(void) {
    arm_cpu_t c;

    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe590f000u);               /* LDR pc,[r0] */
    m_w32(NULL, 0x800u, 0x102u);                /* ARM target with bit 1 set */
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.r[0] = 0x800u;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[15] == 0u,
          "LDR pc accepted the impossible ARM-state 0b10 target");

    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe8b08000u);               /* LDMIA r0!,{pc} */
    m_w32(NULL, 0x800u, 0x102u);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.r[0] = 0x800u;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[15] == 0u && c.r[0] == 0x800u,
          "plain LDM pc accepted 0b10 or committed writeback before rejection");
}

static void test_unaligned_ldr_to_pc_never_reads_memory(void) {
    for (unsigned a = 0; a < 2u; a++) {
        for (unsigned u = 0; u < 2u; u++) {
            arm_cpu_t c;
            memset(g_ram, 0, sizeof g_ram);
            m_w32(NULL, 0u, 0xe590f000u);       /* LDR pc,[r0] */
            arm_reset(&c, &g_bus);
            c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
            c.cp15.sctlr &= ~(ARM_SCTLR_A | ARM_SCTLR_U);
            if (a) c.cp15.sctlr |= ARM_SCTLR_A;
            if (u) c.cp15.sctlr |= ARM_SCTLR_U;
            c.r[0] = 0x801u;
            /* The legacy U=0 path would align the bus address down. */
            g_watch_addr = u ? 0x801u : 0x800u;
            g_watch_reads32 = 0u;

            arm_status_t st = arm_step(&c);
            CHECK(g_watch_reads32 == 0u,
                  "unaligned LDR pc read memory with A=%u U=%u", a, u);
            if (a) {
                CHECK(st == ARM_OK &&
                      (c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT &&
                      c.cp15.dfar == 0x801u,
                      "A=1,U=%u LDR pc status=%d dfsr=%x dfar=%08x",
                      u, (int)st, c.cp15.dfsr, c.cp15.dfar);
            } else {
                CHECK(st == ARM_UNDEFINED && c.r[15] == 0u,
                      "A=0,U=%u LDR pc must be UNPREDICTABLE", u);
            }
            g_watch_addr = 0xffffffffu;
        }
    }
}

static void test_imm_dp_not_trapped(void) {
    /* Regression for the extra-load/store mask: MOV r0,#0x90 has imm8 bits 7 and
     * 4 set, but is immediate data-processing (bit25=1) and must execute. */
    uint32_t p[] = { 0xe3a00090 /*MOV r0,#0x90*/ };
    arm_cpu_t c; arm_status_t st = run_status(&c, p, 1, 1);
    CHECK(st == ARM_OK, "status=%d expect ARM_OK for MOV imm", (int)st);
    CHECK(c.r[0] == 0x90, "r0=%08x expect 90", c.r[0]);
}

static void test_banked_sp_per_mode(void) {
    /* r13 is banked per mode: a value written in SVC must not be visible in IRQ,
     * and must come back when we switch back. */
    arm_cpu_t c; arm_reset(&c, &g_bus);
    arm_set_mode(&c, ARM_MODE_SVC);
    c.r[13] = 0xAAAA0000u;
    arm_set_mode(&c, ARM_MODE_IRQ);
    CHECK(c.r[13] != 0xAAAA0000u, "sp_irq leaked sp_svc (%08x)", c.r[13]);
    c.r[13] = 0xBBBB0000u;
    arm_set_mode(&c, ARM_MODE_SVC);
    CHECK(c.r[13] == 0xAAAA0000u, "sp_svc=%08x expect aaaa0000 after switch back", c.r[13]);
    arm_set_mode(&c, ARM_MODE_IRQ);
    CHECK(c.r[13] == 0xBBBB0000u, "sp_irq=%08x expect bbbb0000", c.r[13]);
}

static void test_fiq_banks_r8_r12(void) {
    /* FIQ additionally banks r8-r12. */
    arm_cpu_t c; arm_reset(&c, &g_bus);
    arm_set_mode(&c, ARM_MODE_SVC);
    c.r[8] = 0x1234;
    arm_set_mode(&c, ARM_MODE_FIQ);
    CHECK(c.r[8] != 0x1234, "r8_fiq leaked r8_usr (%08x)", c.r[8]);
    c.r[8] = 0x5678;
    arm_set_mode(&c, ARM_MODE_SVC);
    CHECK(c.r[8] == 0x1234, "r8=%08x expect 1234 restored", c.r[8]);
}

static void test_mrs_reads_cpsr(void) {
    /* MRS r0, CPSR */
    uint32_t p[] = { 0xe10f0000 };
    arm_cpu_t c; load_and_run(&c, p, 1, 1);
    CHECK(c.r[0] == c.cpsr, "r0=%08x expect cpsr=%08x", c.r[0], c.cpsr);
}

static void test_msr_switches_mode(void) {
    /* MOV r0,#0xD2 (IRQ mode, I set) ; MSR CPSR_c, r0 -> mode becomes IRQ */
    uint32_t p[] = { 0xe3a000d2 /*MOV r0,#0xD2*/,
                     0xe121f000 /*MSR CPSR_c, r0*/ };
    arm_cpu_t c; load_and_run(&c, p, 2, 2);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_IRQ,
          "mode=%02x expect IRQ(12)", c.cpsr & ARM_CPSR_MODE_MASK);
}

static void test_swi_enters_svc(void) {
    /* SWI #0 from SYS mode: vectors to 0x08, enters SVC, LR=return, SPSR=old. */
    uint32_t p[] = { 0xef000000 /*SWI #0*/ };
    arm_cpu_t c; load_and_run(&c, p, 1, 1);
    CHECK(c.r[15] == ARM_VEC_SWI, "pc=%08x expect 08 (SWI vector)", c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC,
          "mode=%02x expect SVC(13)", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(c.r[14] == 4, "lr_svc=%08x expect 4 (return address)", c.r[14]);
    CHECK((c.cpsr & ARM_CPSR_I) != 0, "IRQs should be masked on exception entry");
    CHECK((c.spsr[ARM_BANK_SVC] & ARM_CPSR_MODE_MASK) == ARM_MODE_SYS,
          "spsr_svc should record the interrupted SYS mode");
}

static void test_exception_return_restores_mode(void) {
    /* Full round trip: SWI from SYS lands in SVC; the handler pushes LR and
     * returns with LDMIA sp!,{pc}^ which restores CPSR from SPSR (back to SYS). */
    uint32_t p[16] = {0};
    p[0] = 0xef000000;                 /* 0x00: SWI #0 -> vectors to 0x08 */
    p[2] = 0xe3a0dc09;                 /* 0x08: MOV sp,#0x900 (sp_svc)    */
    p[3] = 0xe92d4000;                 /* 0x0c: STMDB sp!,{lr}            */
    p[4] = 0xe8fd8000;                 /* 0x10: LDMIA sp!,{pc}^  (return) */
    arm_cpu_t c; load_and_run(&c, p, 16, 4);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SYS,
          "mode=%02x expect back in SYS(1f)", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(c.r[15] == 4, "pc=%08x expect 4 (returned after the SWI)", c.r[15]);
}

static void test_cp15_reads_midr(void) {
    /* MRC p15,0,r0,c0,c0,0 -> the ARM1176JZF-S main ID. The kernel reads this
     * to identify the CPU it is running on. */
    uint32_t p[] = { 0xee100f10 };
    arm_cpu_t c; load_and_run(&c, p, 1, 1);
    CHECK(c.r[0] == ARM1176_MIDR, "r0=%08x expect %08x (MIDR)", c.r[0], ARM1176_MIDR);
}

static void test_cp15_cpuid_feature_bank(void) {
    /* CP15 c0 CRm=1 and CRm=2 are the ARMv6 CPUID feature identification
     * registers. MIDR[19:16] on the ARM1176JZF-S reads 0xF, which means
     * "the architecture version is described by this bank, not by MIDR" —
     * so a kernel that wants to know what it is running on has to read it. */
    uint32_t p[] = { 0xee100f11 /*MRC p15,0,r0,c0,c1,0  ID_PFR0 */,
                     0xee101f91 /*MRC p15,0,r1,c0,c1,4  ID_MMFR0*/,
                     0xee102f12 /*MRC p15,0,r2,c0,c2,0  ID_ISAR0*/,
                     0xee103f32 /*MRC p15,0,r3,c0,c2,1  ID_ISAR1*/,
                     0xee104f92 /*MRC p15,0,r4,c0,c2,4  ID_ISAR4*/,
                     0xee105f31 /*MRC p15,0,r5,c0,c1,1  ID_PFR1 */ };
    arm_cpu_t c; load_and_run(&c, p, 6, 6);
    CHECK(c.r[0] == ARM1176_ID_PFR0,  "ID_PFR0=%08x expect %08x",  c.r[0], ARM1176_ID_PFR0);
    CHECK(c.r[1] == ARM1176_ID_MMFR0, "ID_MMFR0=%08x expect %08x", c.r[1], ARM1176_ID_MMFR0);
    CHECK(c.r[2] == ARM1176_ID_ISAR0, "ID_ISAR0=%08x expect %08x", c.r[2], ARM1176_ID_ISAR0);
    CHECK(c.r[3] == ARM1176_ID_ISAR1, "ID_ISAR1=%08x expect %08x", c.r[3], ARM1176_ID_ISAR1);
    CHECK(c.r[4] == ARM1176_ID_ISAR4, "ID_ISAR4=%08x expect %08x", c.r[4], ARM1176_ID_ISAR4);
    CHECK(c.r[5] == ARM1176_ID_PFR1,  "ID_PFR1=%08x expect %08x",  c.r[5], ARM1176_ID_PFR1);
}

static void test_cp15_cpuid_scheme_grades_as_armv6(void) {
    /*
     * This is xnu-1357.5.30's do_cpuid(), lifted verbatim from the kernel at
     * 0xc006257c and run against our CP15. It reads MIDR, notices the 0xF
     * architecture field, reads ID_ISAR1, and if the Jazelle field says 2 it
     * rewrites the architecture field to 7 (ARMv6).
     *
     * cpu_init() then indexes a jump table with (arch - 2) and stores
     * CPU_SUBTYPE_ARM_V6 for arch 7; anything it does not recognise stores
     * CPU_SUBTYPE_ARM_ALL (0), and grade_binary() rejects every ARMv6 Mach-O
     * on the disk with EBADARCH. If this assertion ever fails again, /sbin/launchd
     * stops exec'ing.
     */
    uint32_t p[] = { 0xee101f10 /*MRC p15,0,r1,c0,c0,0   MIDR         */,
                     0xee103f32 /*MRC p15,0,r3,c0,c2,1   ID_ISAR1     */,
                     0xe1a03423 /*LSR   r3, r3, #8                    */,
                     0xe20330f0 /*AND   r3, r3, #0xf0                 */,
                     0xe3530020 /*CMP   r3, #0x20        Jazelle == 2 */,
                     0x03c13702 /*BICEQ r3, r1, #0x80000              */,
                     0x03833807 /*ORREQ r3, r3, #0x70000              */ };
    arm_cpu_t c; load_and_run(&c, p, 7, 7);
    CHECK(((c.r[1] >> 16) & 0xfu) == 0xfu,
          "MIDR arch nibble=%x expect f (CPUID scheme)", (c.r[1] >> 16) & 0xfu);
    CHECK(((c.r[3] >> 16) & 0xfu) == 7u,
          "fixed-up arch nibble=%x expect 7 (ARMv6) — cpu_subtype would be ARM_ALL",
          (c.r[3] >> 16) & 0xfu);
}

static void test_cp15_id_dfr0_matches_absent_debug_unit(void) {
    /* We do not model the CP14 debug unit, so DBGDIDR reads zero. ID_DFR0 must
     * agree and report "no debug architecture": XNU's do_debugid() treats a
     * non-zero ID_DFR0 as licence to publish a breakpoint count derived from
     * DBGDIDR, so the two answers have to be consistent with each other. */
    uint32_t p[] = { 0xee100f51 /*MRC p15,0,r0,c0,c1,2   ID_DFR0 */,
                     0xee101e10 /*MRC p14,0,r1,c0,c0,0   DBGDIDR */ };
    arm_cpu_t c; load_and_run(&c, p, 2, 2);
    CHECK(c.r[0] == 0, "ID_DFR0=%08x expect 0 (no debug unit modelled)", c.r[0]);
    CHECK(c.r[1] == 0, "DBGDIDR=%08x expect 0", c.r[1]);
}

static void test_cp15_sctlr_roundtrip(void) {
    /* Deliberately write SCTLR.C (data cache) rather than SCTLR.M: setting M
     * would switch the MMU on mid-program and, with no page tables loaded, the
     * very next fetch would legitimately take a prefetch abort. */
    uint32_t p[] = { 0xe3a00004 /*MOV r0,#4 (SCTLR.C)*/,
                     0xee010f10 /*MCR p15,0,r0,c1,c0,0*/,
                     0xee111f10 /*MRC p15,0,r1,c1,c0,0*/ };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.r[1] == 4, "r1=%08x expect 4 (SCTLR readback)", c.r[1]);
    CHECK((c.cp15.sctlr & ARM_SCTLR_C) != 0, "SCTLR.C should be set");
}

static void test_cp15_c1_is_gated_on_crm_zero(void) {
    /* c1 with CRm != 0 is the ARM1176JZF-S TrustZone bank — Secure
     * Configuration, Secure Debug Enable, Non-Secure Access Control — which
     * this core does not model. Keying only on opcode_2 aliased that bank onto
     * SCTLR/ACTLR/CPACR, so MCR p15,0,Rd,c1,c1,0 replaced SCTLR wholesale; a
     * cleared SCTLR.U silently swaps the machine from the ARMv6 unaligned model
     * to the legacy rotate one, corrupting data rather than faulting. */
    uint32_t p[] = { 0xe3a00004 /*MOV r0,#4               */,
                     0xee010f10 /*MCR p15,0,r0,c1,c0,0 SCTLR = 4  */,
                     0xe3a00080 /*MOV r0,#0x80            */,
                     0xee010f50 /*MCR p15,0,r0,c1,c0,2 CPACR = 0x80*/,
                     0xe3a00020 /*MOV r0,#0x20            */,
                     0xee010f11 /*MCR p15,0,r0,c1,c1,0 (SCR)      */,
                     0xee010f51 /*MCR p15,0,r0,c1,c1,2 (NSACR)    */,
                     0xee111f11 /*MRC p15,0,r1,c1,c1,0            */,
                     0xee112f10 /*MRC p15,0,r2,c1,c0,0            */ };
    arm_cpu_t c; load_and_run(&c, p, 9, 9);
    CHECK(c.cp15.sctlr == 4u,
          "sctlr=%08x expect 4 — c1,c1,0 must not write SCTLR", c.cp15.sctlr);
    CHECK(c.cp15.cpacr == 0x80u,
          "cpacr=%08x expect 80 — c1,c1,2 must not write CPACR", c.cp15.cpacr);
    CHECK(c.r[1] == 0u,
          "r1=%08x expect 0 — c1,c1,0 must not read SCTLR back", c.r[1]);
    CHECK(c.r[2] == 4u,
          "r2=%08x expect 4 — SCTLR is still readable at CRm==0", c.r[2]);
}

static void test_cp15_ttbr0_roundtrip(void) {
    /* Translation table base survives a write/read cycle (needed for the MMU). */
    uint32_t p[] = { 0xe3a00b02 /*MOV r0,#0x800*/,
                     0xee020f10 /*MCR p15,0,r0,c2,c0,0  (TTBR0)*/,
                     0xee121f10 /*MRC p15,0,r1,c2,c0,0*/ };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.cp15.ttbr0 == 0x800, "ttbr0=%08x expect 800", c.cp15.ttbr0);
    CHECK(c.r[1] == 0x800, "r1=%08x expect 800", c.r[1]);
}

static void test_cp15_ttbcr_masks_only_reserved_bits(void) {
    /* PD1, PD0 and N are writable on ARM1176. Bit 3 and [31:6] are SBZ/UNP. */
    uint32_t p[] = { 0xe3a000ff /*MOV r0,#0xff*/,
                     0xee020f50 /*MCR p15,0,r0,c2,c0,2 (TTBCR)*/,
                     0xee121f50 /*MRC p15,0,r1,c2,c0,2*/ };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.cp15.ttbcr == 0x37u, "ttbcr=%08x expect 00000037", c.cp15.ttbcr);
    CHECK(c.r[1] == 0x37u, "TTBCR readback=%08x expect 00000037", c.r[1]);
}

static void test_cp15_cache_op_is_accepted(void) {
    /* MCR p15,0,r0,c7,c5,0 (invalidate I-cache) must not trap. */
    uint32_t p[] = { 0xee070f15 };
    arm_cpu_t c; arm_status_t st = run_status(&c, p, 1, 1);
    CHECK(st == ARM_OK, "status=%d expect ARM_OK for cache maintenance", (int)st);
}

static void test_cp15_wfi_uses_only_the_exact_privileged_hook(void) {
    /* ARM1176's legacy WFI coordinates: MCR p15,0,<Rd>,c7,c0,4, with
     * the VALUE in Rd SBZ.  The platform callback returning false must not turn
     * a CPU-only harness into a hang; the one WFI still retires and advances
     * to the following instruction. */
    const uint32_t wfi = 0xee070f90u;
    unsigned calls = 0;
    arm_bus_t bus = g_bus;
    bus.ctx = &calls;
    bus.wait_for_interrupt = count_wfi;

    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, wfi);
    arm_cpu_t c;
    arm_reset(&c, &bus);
    c.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F | ARM_CPSR_N;
    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_OK && calls == 1, "status=%d calls=%u expect OK/1",
          (int)st, calls);
    CHECK(c.r[15] == 4u && c.cycles == 1u,
          "pc=%08x cycles=%llu expect one retired WFI at pc 0",
          c.r[15], (unsigned long long)c.cycles);
    CHECK(c.cpsr == (ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F | ARM_CPSR_N),
          "WFI changed CPSR to %08x", c.cpsr);

    /* This is the exact instruction in iPhone OS 3.1.3's _cpu_idle: XNU clears
     * r2, executes DSB through c7,c10,4, then WFI through c7,c0,4.  "Rd SBZ"
     * describes r2's transferred value; it does not require the r0 field. */
    calls = 0;
    m_w32(NULL, 0, 0xee072f90u);                /* MCR p15,0,r2,c7,c0,4 */
    arm_reset(&c, &bus);
    c.cpsr = ARM_MODE_SYS;
    c.r[2] = 0u;
    st = arm_step(&c);
    CHECK(st == ARM_OK && calls == 1 && c.r[15] == 4u,
          "XNU r2 WFI status=%d calls=%u pc=%08x",
          (int)st, calls, c.r[15]);

    /* No platform hook preserves the flat core's historical accepted-no-op
     * behavior.  This is intentional: a standalone CPU has no clock source it
     * could safely advance and must not block its host indefinitely. */
    m_w32(NULL, 0, wfi);
    arm_reset(&c, &g_bus);
    c.cpsr = ARM_MODE_SYS;
    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[15] == 4u && c.cycles == 1u,
          "hookless WFI status=%d pc=%08x cycles=%llu",
          (int)st, c.r[15], (unsigned long long)c.cycles);

    /* WFI is privileged-only.  User mode must fault before the hook can make
     * any platform state advance. */
    calls = 0;
    m_w32(NULL, 0, wfi);
    arm_reset(&c, &bus);
    c.cpsr = ARM_MODE_USR;
    st = arm_step(&c);
    CHECK(st == ARM_UNDEFINED && calls == 0,
          "user WFI status=%d calls=%u expect undefined/0", (int)st, calls);

    /* A non-zero transferred value violates SBZ and must fail before changing
     * platform time. */
    calls = 0;
    m_w32(NULL, 0, 0xee072f90u);
    arm_reset(&c, &bus);
    c.cpsr = ARM_MODE_SYS;
    c.r[2] = 1u;
    st = arm_step(&c);
    CHECK(st == ARM_UNDEFINED && calls == 0,
          "non-zero WFI operand status=%d calls=%u expect undefined/0",
          (int)st, calls);

    /* An MRC and a non-zero Opcode_1 at the same c7 coordinates are not the
     * WFI operation and must not invoke the wait hook. */
    const uint32_t not_wfi[] = { 0xee170f90u, 0xee270f90u };
    for (unsigned i = 0; i < sizeof not_wfi / sizeof not_wfi[0]; i++) {
        calls = 0;
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, not_wfi[i]);
        arm_reset(&c, &bus);
        c.cpsr = ARM_MODE_SYS;
        st = arm_step(&c);
        CHECK(calls == 0, "non-WFI encoding %08x called hook %u times",
              not_wfi[i], calls);
    }
}

static void test_high_vectors(void) {
    /* Setting SCTLR.V (bit 13) moves the vector table to 0xFFFF0000, so a SWI
     * must vector to 0xFFFF0008 instead of 0x8. */
    uint32_t p[] = { 0xe3a00a02 /*MOV r0,#0x2000 (SCTLR.V)*/,
                     0xee010f10 /*MCR p15,0,r0,c1,c0,0*/,
                     0xef000000 /*SWI #0*/ };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.r[15] == 0xffff0008u, "pc=%08x expect ffff0008 (high vectors)", c.r[15]);
}

/* ---------------------------------------------------------------- MMU tests */
/* Build a first-level table at 0x4000 mapping one 1 MB section, exactly as a
 * kernel would lay it out in RAM, then let the real walker translate. */
static void mmu_setup_section(arm_cpu_t *c, uint32_t va, uint32_t pa,
                              unsigned ap, unsigned domain) {
    const uint32_t l1 = 0x4000;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, l1 + ((va >> 20) << 2),
          (pa & 0xfff00000u) | (ap << 10) | (domain << 5) | 2u); /* section */
    arm_reset(c, &g_bus);
    c->cp15.ttbr0 = l1;
    c->cp15.dacr  = 1u << (domain * 2);       /* client: check AP */
    c->cp15.sctlr |= ARM_SCTLR_M;             /* MMU on */
}

static void test_mmu_disabled_is_identity(void) {
    arm_cpu_t c; arm_reset(&c, &g_bus);
    uint32_t pa = 0;
    uint32_t f = arm_mmu_translate(&c, 0xdeadb000u, ARM_ACCESS_READ, true, &pa);
    CHECK(f == 0, "fsr=%u expect 0 with MMU off", f);
    CHECK(pa == 0xdeadb000u, "pa=%08x expect identity", pa);
}

static void test_mmu_section_translation(void) {
    arm_cpu_t c; mmu_setup_section(&c, 0x80000000u, 0x00200000u, 3, 0);
    uint32_t pa = 0;
    uint32_t f = arm_mmu_translate(&c, 0x80001234u, ARM_ACCESS_READ, true, &pa);
    CHECK(f == 0, "fsr=%u expect 0", f);
    CHECK(pa == 0x00201234u, "pa=%08x expect 00201234", pa);
}

static void test_mmu_unmapped_faults(void) {
    arm_cpu_t c; mmu_setup_section(&c, 0x80000000u, 0x00200000u, 3, 0);
    uint32_t pa = 0;
    /* 0x90000000 has no descriptor at all. */
    uint32_t f = arm_mmu_translate(&c, 0x90000000u, ARM_ACCESS_READ, true, &pa);
    CHECK((f & 0xf) == ARM_FSR_SECTION_TRANSLATION,
          "fsr=%x expect section translation fault", f);
}

static void test_mmu_user_write_permission(void) {
    /* AP=10: privileged RW, user read-only. A user write must fault. */
    arm_cpu_t c; mmu_setup_section(&c, 0x80000000u, 0x00200000u, 2, 0);
    uint32_t pa = 0;
    CHECK(arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, false, &pa) == 0,
          "user read should be permitted with AP=10");
    uint32_t f = arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_WRITE, false, &pa);
    CHECK((f & 0xf) == ARM_FSR_SECTION_PERMISSION,
          "fsr=%x expect permission fault on user write", f);
}

static void test_mmu_small_page_translation(void) {
    /* Two-level walk: L1 coarse pointer -> L2 small page. */
    const uint32_t l1 = 0x4000, l2 = 0x5000;
    arm_cpu_t c;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, l1 + ((0x80000000u >> 20) << 2), (l2 & 0xfffffc00u) | 1u); /* coarse */
    m_w32(NULL, l2 + (((0x80000000u >> 12) & 0xff) << 2),
          (0x00300000u & 0xfffff000u) | (3u << 4) | 2u);                   /* small page, AP=11 */
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = l1; c.cp15.dacr = 1u;
    c.cp15.sctlr |= ARM_SCTLR_M | ARM_SCTLR_XP;
    uint32_t pa = 0;
    uint32_t f = arm_mmu_translate(&c, 0x80000abcu, ARM_ACCESS_READ, true, &pa);
    CHECK(f == 0, "fsr=%u expect 0 for small page", f);
    CHECK(pa == 0x00300abcu, "pa=%08x expect 00300abc", pa);
}

static void test_data_abort_taken(void) {
    /* With the MMU on and nothing mapped at the load address, LDR must raise a
     * data abort: ABT mode, vector 0x10, and DFAR recording the address. */
    arm_cpu_t c;
    memset(g_ram, 0, sizeof g_ram);
    /* identity-map the low section so the fetch itself succeeds */
    m_w32(NULL, 0x4000 + 0, (0x00000000u) | (3u << 10) | 2u);
    /* 0x90000000 is in a different 1 MB section than the identity-mapped one,
     * so it has no descriptor at all. */
    uint32_t prog[] = { 0xe3a01209 /*MOV r1,#0x90000000*/, 0xe5912000 /*LDR r2,[r1]*/ };
    for (unsigned i = 0; i < 2; i++) m_w32(NULL, i * 4, prog[i]);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.cp15.ttbr0 = 0x4000; c.cp15.dacr = 1u; c.cp15.sctlr |= ARM_SCTLR_M;
    arm_step(&c); arm_step(&c);
    CHECK(c.r[15] == ARM_VEC_DATA_ABORT, "pc=%08x expect 10 (data abort)", c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_ABT,
          "mode=%02x expect ABT(17)", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(c.cp15.dfar == 0x90000000u, "dfar=%08x expect 90000000", c.cp15.dfar);
}

/* --- regressions from the adversarial audit ----------------------------- */

static void test_pld_is_a_nop_not_a_branch(void) {
    /* cond==0xF is the ARMv6 unconditional space. PLD must be a no-op; if it is
     * decoded as a conditional instruction it becomes "LDRB pc,[r1]" and
     * branches to whatever byte it loaded. */
    uint32_t p[] = { 0xe3a01a01 /*MOV r1,#0x1000*/,
                     0xf5d1f000 /*PLD [r1]*/,
                     0xe3a00007 /*MOV r0,#7*/ };
    arm_cpu_t c; load_and_run(&c, p, 3, 3);
    CHECK(c.r[0] == 7, "r0=%u expect 7 (execution continued past PLD)", c.r[0]);
    CHECK(c.r[15] == 12, "pc=%08x expect 0c (PLD did not branch)", c.r[15]);
}

static void test_unconditional_space_traps(void) {
    /* Unimplemented cond==0xF encodings (e.g. SETEND) must trap, not execute. */
    uint32_t p[] = { 0xf1010200 /*SETEND BE*/ };
    arm_cpu_t c; arm_status_t st = run_status(&c, p, 1, 1);
    CHECK(st == ARM_UNDEFINED, "status=%d expect ARM_UNDEFINED for SETEND", (int)st);
}

static void test_clz_does_not_corrupt_cpsr(void) {
    /* CLZ sits in the same opcode space as MSR. With a loose MSR mask it would
     * rewrite CPSR (including the mode field) instead of trapping. */
    uint32_t p[] = { 0xe16f0f11 /*CLZ r0,r1*/ };
    arm_cpu_t c; arm_reset(&c, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, p[0]);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.r[1] = 1;
    uint32_t before = c.cpsr;
    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_OK, "status=%d expect ARM_OK — CLZ is implemented", (int)st);
    CHECK(c.r[0] == 31, "r0=%u expect 31 (clz of 1)", c.r[0]);
    CHECK(c.cpsr == before, "cpsr changed %08x -> %08x (CLZ decoded as MSR)",
          before, c.cpsr);
}

static void test_msr_still_works(void) {
    /* The tightened mask must not break real MSR. */
    uint32_t p[] = { 0xe3a000d2 /*MOV r0,#0xD2*/, 0xe121f000 /*MSR CPSR_c,r0*/ };
    arm_cpu_t c; load_and_run(&c, p, 2, 2);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_IRQ,
          "mode=%02x expect IRQ", c.cpsr & ARM_CPSR_MODE_MASK);
}

static void test_arm_media_extend_and_reverse(void) {
    /* The ARM media extend and byte-reverse families, which real XNU uses in
     * ordinary compiled code. (These used to trap; that assertion documented a
     * limitation that no longer exists.) */
    uint32_t p[] = { 0xe59f1010 /*LDR r1,[pc,#16] -> the literal at 0x18*/,
                     0xe6bf0f31 /*REV  r0,r1 */,
                     0xe6ef2071 /*UXTB r2,r1 */,
                     0xe6bf3071 /*SXTH r3,r1 */,
                     0xe6ff4071 /*UXTH r4,r1 */,
                     0xeafffffe /*B .        */,
                     0x11228344 /*literal    */ };
    arm_cpu_t c; load_and_run(&c, p, 7, 5);
    CHECK(c.r[1] == 0x11228344u, "r1=%08x expect 11228344", c.r[1]);
    CHECK(c.r[0] == 0x44832211u, "r0=%08x expect 44832211 (REV)", c.r[0]);
    CHECK(c.r[2] == 0x44, "r2=%08x expect 44 (UXTB)", c.r[2]);
    CHECK(c.r[3] == 0xffff8344u, "r3=%08x expect ffff8344 (SXTH sign-extends)", c.r[3]);
    CHECK(c.r[4] == 0x8344, "r4=%08x expect 8344 (UXTH)", c.r[4]);
}

static void test_arm_media_pair_extend(void) {
    /* cccc 0110 1 op Rn Rd rot00 0111 Rm; op 8 is signed, C unsigned.
     * Rn=PC names the plain form. These cases cover the exact real-userland
     * blocker plus both lane signs, every rotation, lane-local wraparound,
     * aliases, conditions, flag preservation and invalid PC operands. */
#define PAIR_EXT(cond, op, rn, rd, rot, rm) \
    (((cond) << 28) | 0x06000070u | ((op) << 20) | ((rn) << 16) | \
     ((rd) << 12) | ((rot) << 10) | (rm))
    arm_cpu_t c;
    arm_status_t st;
    const uint32_t blocker = 0xe6cf3073u;       /* UXTB16 r3,r3 */

    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, blocker);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS
           | ARM_CPSR_N | ARM_CPSR_C | ARM_CPSR_Q | 0x000f0000u;
    c.r[3] = 0x00000100u;                  /* live register value at the stop */
    uint32_t flags = c.cpsr;
    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[3] == 0u && c.r[15] == 4u,
          "real UXTB16 blocker status=%d r3=%08x pc=%08x",
          (int)st, c.r[3], c.r[15]);
    CHECK(c.cpsr == flags, "UXTB16 changed CPSR %08x -> %08x", flags, c.cpsr);

    struct pair_case { uint32_t insn, src, base, expect; const char *name; } cases[] = {
        { PAIR_EXT(0xeu,0xcu,15u,0u,0u,1u), 0x80ff7f01u, 0u,
          0x00ff0001u, "UXTB16 ror0" },
        { PAIR_EXT(0xeu,0x8u,15u,0u,0u,1u), 0x80ff7f01u, 0u,
          0xffff0001u, "SXTB16 ror0" },
        { PAIR_EXT(0xeu,0x8u,15u,0u,1u,1u), 0x80ff7f01u, 0u,
          0xff80007fu, "SXTB16 ror8" },
        { PAIR_EXT(0xeu,0x8u,15u,0u,2u,1u), 0x80ff7f01u, 0u,
          0x0001ffffu, "SXTB16 ror16" },
        { PAIR_EXT(0xeu,0xcu,15u,0u,3u,1u), 0x80ff7f01u, 0u,
          0x007f0080u, "UXTB16 ror24" },
        { PAIR_EXT(0xeu,0xcu,2u,0u,0u,1u),  0x00ff0020u, 0xfffffff0u,
          0x00fe0010u, "UXTAB16 lane-local wrap" },
        { PAIR_EXT(0xeu,0x8u,2u,0u,0u,1u),  0x008000ffu, 0x00010001u,
          0xff810000u, "SXTAB16 signed lanes" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, cases[i].insn);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS
               | ARM_CPSR_Z | ARM_CPSR_V | ARM_CPSR_Q | 0x000a0000u;
        c.r[1] = cases[i].src;
        c.r[2] = cases[i].base;
        flags = c.cpsr;
        st = arm_step(&c);
        CHECK(st == ARM_OK && c.r[0] == cases[i].expect,
              "%s status=%d got=%08x expect=%08x", cases[i].name,
              (int)st, c.r[0], cases[i].expect);
        CHECK(c.cpsr == flags, "%s changed CPSR %08x -> %08x",
              cases[i].name, flags, c.cpsr);
    }

    /* The scalar accumulate forms share the tightened family mask. Exercise
     * every op with a non-zero rotation so mask or dispatch changes cannot
     * regress them while the paired cases stay green. */
    struct scalar_case { uint32_t insn, src, base, expect; const char *name; } scalar[] = {
        { PAIR_EXT(0xeu,0xau,2u,0u,1u,1u), 0x80ff7f01u, 0x00000100u,
          0x0000017fu, "SXTAB ror8" },
        { PAIR_EXT(0xeu,0xbu,2u,0u,2u,1u), 0x80ff7f01u, 0x00010000u,
          0x000080ffu, "SXTAH ror16" },
        { PAIR_EXT(0xeu,0xeu,2u,0u,3u,1u), 0x80ff7f01u, 0xfffffff0u,
          0x00000070u, "UXTAB ror24" },
        { PAIR_EXT(0xeu,0xfu,2u,0u,1u,1u), 0x80ff7f01u, 0x00010000u,
          0x0001ff7fu, "UXTAH ror8" },
    };
    for (size_t i = 0; i < sizeof scalar / sizeof scalar[0]; i++) {
        memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, scalar[i].insn);
        arm_reset(&c, &g_bus); c.r[1] = scalar[i].src; c.r[2] = scalar[i].base;
        c.cpsr |= ARM_CPSR_N | ARM_CPSR_V | ARM_CPSR_Q | 0x00050000u;
        flags = c.cpsr;
        st = arm_step(&c);
        CHECK(st == ARM_OK && c.r[0] == scalar[i].expect,
              "%s status=%d got=%08x expect=%08x", scalar[i].name,
              (int)st, c.r[0], scalar[i].expect);
        CHECK(c.cpsr == flags, "%s changed CPSR %08x -> %08x",
              scalar[i].name, flags, c.cpsr);
    }

    /* Rd aliases Rn: both source halves must be captured before the write. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, PAIR_EXT(0xeu,0xcu,2u,2u,0u,1u));
    arm_reset(&c, &g_bus); c.r[1] = 0x00020003u; c.r[2] = 0xfffffff0u;
    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[2] == 0x0001fff3u,
          "UXTAB16 Rd==Rn status=%d r2=%08x", (int)st, c.r[2]);

    /* Rd aliases Rm: the complete rotated source must be captured first. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, PAIR_EXT(0xeu,0x8u,2u,1u,1u,1u));
    arm_reset(&c, &g_bus); c.r[1] = 0x80ff7f01u; c.r[2] = 0x00010001u;
    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[1] == 0xff810080u,
          "SXTAB16 Rd==Rm status=%d r1=%08x", (int)st, c.r[1]);

    /* All operands may name one register; the old source and both base lanes
     * must be captured before the destination write. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, PAIR_EXT(0xeu,0xcu,1u,1u,0u,1u));
    arm_reset(&c, &g_bus); c.r[1] = 0x00020003u;
    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[1] == 0x00040006u,
          "UXTAB16 Rn==Rd==Rm status=%d r1=%08x", (int)st, c.r[1]);

    /* Failed condition leaves destination, flags and PC progression ordinary. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, PAIR_EXT(0x0u,0xcu,15u,0u,0u,1u)); /* UXTB16EQ */
    arm_reset(&c, &g_bus); c.r[0] = 0xfeedfaceu; c.r[1] = 0xffffffffu;
    c.cpsr &= ~ARM_CPSR_Z;
    flags = c.cpsr;
    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[0] == 0xfeedfaceu && c.r[15] == 4u &&
          c.cpsr == flags, "failed-condition UXTB16 mutated state");

    /* PC is not a legal destination or byte source for any extend form. Check
     * the paired encodings and the previously implemented scalar encodings. */
    uint32_t bad[] = {
        PAIR_EXT(0xeu,0x8u,15u,15u,0u,1u),
        PAIR_EXT(0xeu,0xcu,15u,0u,0u,15u),
        PAIR_EXT(0xeu,0xau,15u,15u,0u,1u),
        PAIR_EXT(0xeu,0xeu,15u,0u,0u,15u),
        0xe6cf2171u,                       /* reserved bits[9:8] != 00 */
        PAIR_EXT(0xeu,0x9u,15u,0u,0u,1u), /* unallocated op */
        PAIR_EXT(0xeu,0xdu,15u,0u,0u,1u), /* unallocated op */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        st = run_status(&c, &bad[i], 1, 1);
        CHECK(st == ARM_UNDEFINED, "bad extend encoding %08x status=%d",
              bad[i], (int)st);
    }
#undef PAIR_EXT
}

static void test_arm_media_reverse_edges(void) {
    struct reverse_case { uint32_t insn, expect; const char *name; } cases[] = {
        { 0xe6bf0f31u, 0x44832211u, "REV" },
        { 0xe6bf0fb1u, 0x22114483u, "REV16" },
        { 0xe6ff0fb1u, 0x00004483u, "REVSH positive" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        arm_cpu_t c;
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, cases[i].insn);
        arm_reset(&c, &g_bus);
        c.r[1] = 0x11228344u;
        c.cpsr |= ARM_CPSR_N | ARM_CPSR_C | ARM_CPSR_Q | 0x000f0000u;
        uint32_t flags = c.cpsr;
        arm_status_t st = arm_step(&c);
        CHECK(st == ARM_OK && c.r[0] == cases[i].expect,
              "%s status=%d got=%08x expect=%08x", cases[i].name,
              (int)st, c.r[0], cases[i].expect);
        CHECK(c.cpsr == flags, "%s changed CPSR %08x -> %08x",
              cases[i].name, flags, c.cpsr);

        uint32_t bad_rd = cases[i].insn | 0x0000f000u;
        uint32_t bad_rm = (cases[i].insn & ~0xfu) | 0xfu;
        CHECK(run_status(&c, &bad_rd, 1, 1) == ARM_UNDEFINED,
              "%s accepted Rd=PC encoding %08x", cases[i].name, bad_rd);
        CHECK(run_status(&c, &bad_rm, 1, 1) == ARM_UNDEFINED,
              "%s accepted Rm=PC encoding %08x", cases[i].name, bad_rm);
    }

    /* REVSH must sign-extend bit 15 of the byte-reversed halfword. */
    uint32_t revsh = 0xe6ff0fb1u;
    arm_cpu_t c;
    memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, revsh);
    arm_reset(&c, &g_bus); c.r[1] = 0x11224483u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0xffff8344u,
          "REVSH negative got=%08x expect ffff8344", c.r[0]);

    /* Destination/source aliasing is legal, and a failed condition must be a
     * pure no-op apart from ordinary sequential PC progression. */
    uint32_t rev_alias = 0xe6bf1f31u;       /* REV r1,r1 */
    memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, rev_alias);
    arm_reset(&c, &g_bus); c.r[1] = 0x11228344u;
    CHECK(arm_step(&c) == ARM_OK && c.r[1] == 0x44832211u,
          "REV Rd==Rm got=%08x", c.r[1]);

    uint32_t rev_eq = 0x06bf0f31u;          /* REVEQ r0,r1 */
    memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, rev_eq);
    arm_reset(&c, &g_bus); c.cpsr &= ~ARM_CPSR_Z;
    c.r[0] = 0xfeedfaceu; c.r[1] = 0x11228344u;
    uint32_t before = c.cpsr;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0xfeedfaceu &&
          c.r[15] == 4u && c.cpsr == before,
          "failed-condition REV mutated state");
}

static void test_arm_media_saturate(void) {
#define SAT(cond, uns, field, rd, shift, asr, rn) \
    (((cond) << 28) | 0x06a00010u | ((uns) << 22) | ((field) << 16) | \
     ((rd) << 12) | ((shift) << 7) | ((asr) << 6) | (rn))
#define SAT16(cond, uns, field, rd, rn) \
    (((cond) << 28) | 0x06a00f30u | ((uns) << 22) | ((field) << 16) | \
     ((rd) << 12) | (rn))
    arm_cpu_t c;
    arm_status_t st;

    /* Both observed UI stops used this exact word. ASR #14 produces 256 here,
     * which USAT #8 must clamp to 255 and report through sticky Q. */
    const uint32_t blocker = 0xe6e83756u;       /* USAT r3,#8,r6,ASR #14 */
    memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, blocker);
    arm_reset(&c, &g_bus);
    c.cpsr = ARM_MODE_SYS | ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C |
             ARM_CPSR_V | 0x000a0000u;
    c.r[6] = 0x00400000u;
    uint32_t preserved = c.cpsr;
    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[3] == 0xffu && c.r[15] == 4u,
          "real USAT blocker status=%d r3=%08x pc=%08x",
          (int)st, c.r[3], c.r[15]);
    CHECK(c.cpsr == (preserved | ARM_CPSR_Q),
          "real USAT blocker flags=%08x expect=%08x",
          c.cpsr, preserved | ARM_CPSR_Q);

    static const struct {
        uint32_t insn, source, expect;
        bool expect_q;
        const char *what;
    } scalar[] = {
        { SAT(0xeu,1u,8u,0u,0u,0u,1u), 0xffffffffu, 0u, true,
          "USAT clamps a negative input to zero" },
        { SAT(0xeu,1u,8u,0u,0u,0u,1u), 0x000000ffu, 0x000000ffu, false,
          "USAT keeps its upper boundary" },
        { SAT(0xeu,1u,8u,0u,0u,0u,1u), 0x00000100u, 0x000000ffu, true,
          "USAT clamps above its upper boundary" },
        { SAT(0xeu,1u,0u,0u,0u,0u,1u), 1u, 0u, true,
          "USAT zero-bit range contains only zero" },
        { SAT(0xeu,0u,7u,0u,0u,0u,1u), 0xffffff7fu, 0xffffff80u, true,
          "SSAT clamps below minus 128" },
        { SAT(0xeu,0u,7u,0u,0u,0u,1u), 0xffffff80u, 0xffffff80u, false,
          "SSAT keeps its negative boundary" },
        { SAT(0xeu,0u,7u,0u,0u,0u,1u), 0x00000080u, 0x0000007fu, true,
          "SSAT clamps above 127" },
        { SAT(0xeu,0u,31u,0u,0u,0u,1u), 0x80000000u, 0x80000000u, false,
          "SSAT 32-bit range keeps INT_MIN" },
        { SAT(0xeu,1u,8u,0u,0u,1u,1u), 0x80000000u, 0u, true,
          "USAT ASR immediate zero means shift by 32" },
        { SAT(0xeu,1u,8u,0u,31u,0u,1u), 1u, 0u, true,
          "USAT saturates the signed result after LSL" },
    };
    for (size_t i = 0; i < sizeof scalar / sizeof scalar[0]; i++) {
        memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, scalar[i].insn);
        arm_reset(&c, &g_bus);
        c.cpsr = ARM_MODE_SYS | ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C |
                 ARM_CPSR_V | 0x00050000u;
        preserved = c.cpsr;
        c.r[1] = scalar[i].source;
        st = arm_step(&c);
        CHECK(st == ARM_OK && c.r[0] == scalar[i].expect,
              "%s status=%d got=%08x expect=%08x", scalar[i].what,
              (int)st, c.r[0], scalar[i].expect);
        CHECK(c.cpsr == (preserved | (scalar[i].expect_q ? ARM_CPSR_Q : 0u)),
              "%s flags=%08x", scalar[i].what, c.cpsr);
    }

    /* Q is sticky and Rd may alias Rn. */
    uint32_t alias = SAT(0xeu,1u,8u,1u,0u,0u,1u);
    memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, alias);
    arm_reset(&c, &g_bus); c.cpsr |= ARM_CPSR_Q | ARM_CPSR_Z;
    c.r[1] = 42u; preserved = c.cpsr;
    CHECK(arm_step(&c) == ARM_OK && c.r[1] == 42u && c.cpsr == preserved,
          "non-saturating aliased USAT changed value or sticky flags");

    static const struct {
        uint32_t insn, source, expect;
        bool expect_q;
        const char *what;
    } lanes[] = {
        { SAT16(0xeu,0u,7u,0u,1u), 0xff7f0080u, 0xff80007fu, true,
          "SSAT16 clamps each signed half independently" },
        { SAT16(0xeu,0u,7u,0u,1u), 0x007fffffu, 0x007fffffu, false,
          "SSAT16 keeps in-range signed halves" },
        { SAT16(0xeu,1u,8u,0u,1u), 0xffff012cu, 0x000000ffu, true,
          "USAT16 clamps negative and oversized halves" },
        { SAT16(0xeu,1u,0u,0u,1u), 0u, 0u, false,
          "USAT16 zero-bit range keeps zero" },
    };
    for (size_t i = 0; i < sizeof lanes / sizeof lanes[0]; i++) {
        memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, lanes[i].insn);
        arm_reset(&c, &g_bus);
        c.cpsr = ARM_MODE_SYS | ARM_CPSR_N | ARM_CPSR_C | 0x000a0000u;
        preserved = c.cpsr; c.r[1] = lanes[i].source;
        st = arm_step(&c);
        CHECK(st == ARM_OK && c.r[0] == lanes[i].expect,
              "%s status=%d got=%08x expect=%08x", lanes[i].what,
              (int)st, c.r[0], lanes[i].expect);
        CHECK(c.cpsr == (preserved | (lanes[i].expect_q ? ARM_CPSR_Q : 0u)),
              "%s flags=%08x", lanes[i].what, c.cpsr);
    }

    /* Failed conditions are ordinary no-ops, and PC is not a legal source or
     * destination for any of the four saturation forms. */
    uint32_t eq = SAT(0u,1u,8u,0u,0u,0u,1u);
    memset(g_ram, 0, sizeof g_ram); m_w32(NULL, 0, eq);
    arm_reset(&c, &g_bus); c.cpsr &= ~ARM_CPSR_Z;
    c.r[0] = 0xfeedfaceu; c.r[1] = 0x100u; preserved = c.cpsr;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0xfeedfaceu &&
          c.r[15] == 4u && c.cpsr == preserved,
          "failed-condition USAT mutated architectural state");

    uint32_t bad[] = {
        SAT(0xeu,0u,7u,15u,0u,0u,1u),
        SAT(0xeu,1u,8u,0u,0u,0u,15u),
        SAT16(0xeu,0u,7u,15u,1u),
        SAT16(0xeu,1u,8u,0u,15u),
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(run_status(&c, &bad[i], 1, 1) == ARM_UNDEFINED,
              "saturate encoding %08x accepted PC operand", bad[i]);

#undef SAT16
#undef SAT
}

static void test_apx_makes_mapping_read_only(void) {
    /* APX=1, AP=01 is privileged read-only: a privileged write must fault. */
    arm_cpu_t c;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x4000 + ((0x80000000u >> 20) << 2),
          0x00200000u | (1u << 15) | (1u << 10) | 2u);   /* APX=1, AP=01 */
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = 0x4000; c.cp15.dacr = 1u;
    c.cp15.sctlr |= ARM_SCTLR_M | ARM_SCTLR_XP;

    uint32_t pa = 0;
    CHECK(arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, true, &pa) == 0,
          "privileged read should be allowed with APX=1,AP=01");
    uint32_t f = arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_WRITE, true, &pa);
    CHECK((f & 0xf) == ARM_FSR_SECTION_PERMISSION,
          "fsr=%x expect permission fault on write to a read-only section", f);
}

static void test_xp0_ignores_extended_apx_bits(void) {
    arm_cpu_t c;
    uint32_t pa = 0;
    memset(g_ram, 0, sizeof g_ram);

    /* In the backwards-compatible format section bit 15 is not APX. Treating
     * it as APX silently turns a writable AP=11 section read-only. */
    m_w32(NULL, 0x4000 + (0x800u << 2),
          0x00200000u | (1u << 15) | (3u << 10) | 2u);
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = 0x4000u;
    c.cp15.dacr = 1u;
    c.cp15.sctlr = ARM_SCTLR_M;                 /* XP=0 */
    CHECK(arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_WRITE, true, &pa) == 0,
          "XP=0 section bit 15 was incorrectly treated as APX");

    /* L2 type 3 is the legacy extended-small-page form. Its bit 9 is likewise
     * not APX, and one AP field controls the whole 4 KB page. */
    m_w32(NULL, 0x4000 + (0x801u << 2), 0x8000u | 1u);
    m_w32(NULL, 0x8000u, 0x00300000u | (1u << 9) | (3u << 4) | 3u);
    /* Probe VA[11:10]=01: a mistaken subpage decode would read the zero AP1
     * pair in bits[7:6], while the actual extended-small format still uses its
     * one AP=11 field in bits[5:4]. */
    CHECK(arm_mmu_translate(&c, 0x80100400u, ARM_ACCESS_WRITE, false, &pa) == 0,
          "XP=0 extended-small single AP/bit-9 semantics were misdecoded");
}

static void test_xp0_large_and_small_ap_subpages(void) {
    arm_cpu_t c;
    uint32_t pa = 0;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x4000 + (0x800u << 2), 0x8000u | 1u);

    /* AP0..AP3 = 11,00,01,10. A 64 KB descriptor is repeated in all sixteen
     * L2 slots, and VA[15:14] selects its 16 KB permission subpage. */
    uint32_t large = 0x00020000u | (3u << 4) | (0u << 6) |
                     (1u << 8) | (2u << 10) | 1u;
    for (unsigned i = 0; i < 16u; i++) m_w32(NULL, 0x8000u + i * 4u, large);
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = 0x4000u;
    c.cp15.dacr = 1u;
    c.cp15.sctlr = ARM_SCTLR_M;                 /* XP=0 */
    CHECK(arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_WRITE, false, &pa) == 0,
          "large-page AP0=11 should allow user writes");
    CHECK((arm_mmu_translate(&c, 0x80004000u, ARM_ACCESS_READ, true, &pa) & 0xfu)
              == ARM_FSR_PAGE_PERMISSION,
          "large-page AP1=00 should deny privileged reads");
    CHECK(arm_mmu_translate(&c, 0x80008000u, ARM_ACCESS_WRITE, true, &pa) == 0,
          "large-page AP2=01 should allow privileged writes");
    CHECK((arm_mmu_translate(&c, 0x80008000u, ARM_ACCESS_READ, false, &pa) & 0xfu)
              == ARM_FSR_PAGE_PERMISSION,
          "large-page AP2=01 should deny user reads");
    CHECK(arm_mmu_translate(&c, 0x8000c000u, ARM_ACCESS_READ, false, &pa) == 0,
          "large-page AP3=10 should allow user reads");
    CHECK((arm_mmu_translate(&c, 0x8000c000u, ARM_ACCESS_WRITE, false, &pa) & 0xfu)
              == ARM_FSR_PAGE_PERMISSION,
          "large-page AP3=10 should deny user writes");

    /* The 4 KB legacy small page uses VA[11:10] to select four 1 KB AP fields. */
    m_w32(NULL, 0x8000u, 0x00030000u | (3u << 4) | (0u << 6) |
                              (1u << 8) | (2u << 10) | 2u);
    CHECK(arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_WRITE, false, &pa) == 0,
          "small-page AP0=11 should allow user writes");
    CHECK((arm_mmu_translate(&c, 0x80000400u, ARM_ACCESS_READ, true, &pa) & 0xfu)
              == ARM_FSR_PAGE_PERMISSION,
          "small-page AP1=00 should deny privileged reads");
    CHECK(arm_mmu_translate(&c, 0x80000800u, ARM_ACCESS_WRITE, true, &pa) == 0,
          "small-page AP2=01 should allow privileged writes");
    CHECK((arm_mmu_translate(&c, 0x80000800u, ARM_ACCESS_READ, false, &pa) & 0xfu)
              == ARM_FSR_PAGE_PERMISSION,
          "small-page AP2=01 should deny user reads");
    CHECK(arm_mmu_translate(&c, 0x80000c00u, ARM_ACCESS_READ, false, &pa) == 0,
          "small-page AP3=10 should allow user reads");
    CHECK((arm_mmu_translate(&c, 0x80000c00u, ARM_ACCESS_WRITE, false, &pa) & 0xfu)
              == ARM_FSR_PAGE_PERMISSION,
          "small-page AP3=10 should deny user writes");
}

static void test_sctlr_sr_legacy_permissions(void) {
    arm_cpu_t c;
    uint32_t pa = 0;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x4000 + (0x800u << 2), 0x00200000u | 2u); /* AP=00 */
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = 0x4000u;
    c.cp15.dacr = 1u;
    c.cp15.sctlr = ARM_SCTLR_M;

    CHECK((arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, true, &pa) & 0xfu)
              == ARM_FSR_SECTION_PERMISSION,
          "AP=00 with S=R=0 should deny access");
    c.cp15.sctlr = ARM_SCTLR_M | ARM_SCTLR_S;
    CHECK(arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, true, &pa) == 0,
          "S alone should permit privileged reads");
    CHECK((arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_WRITE, true, &pa) & 0xfu)
              == ARM_FSR_SECTION_PERMISSION,
          "S alone should deny privileged writes");
    CHECK((arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, false, &pa) & 0xfu)
              == ARM_FSR_SECTION_PERMISSION,
          "S alone should deny user reads");
    c.cp15.sctlr = ARM_SCTLR_M | ARM_SCTLR_R;
    CHECK(arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, true, &pa) == 0 &&
          arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, false, &pa) == 0,
          "R alone should permit reads in both privilege levels");
    CHECK((arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_WRITE, false, &pa) & 0xfu)
              == ARM_FSR_SECTION_PERMISSION,
          "R alone should deny writes");
    c.cp15.sctlr = ARM_SCTLR_M | ARM_SCTLR_S | ARM_SCTLR_R;
    CHECK((arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, true, &pa) & 0xfu)
              == ARM_FSR_SECTION_PERMISSION,
          "reserved S=R=1 must not grant access");
}

static void test_arm1176_rejects_fine_page_tables(void) {
    arm_cpu_t c;
    uint32_t pa = 0;
    memset(g_ram, 0, sizeof g_ram);
    /* ARM1176 removed the old L1 fine-table descriptor. It is a translation
     * fault before any domain lookup, even when its obsolete domain bits name
     * a domain that DACR would reject. */
    m_w32(NULL, 0x4000 + (0x800u << 2), 0x8000u | (7u << 5) | 3u);
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = 0x4000u;
    c.cp15.dacr = 0u;
    c.cp15.sctlr = ARM_SCTLR_M;
    uint32_t f = arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, true, &pa);
    CHECK((f & 0xfu) == ARM_FSR_SECTION_TRANSLATION,
          "L1 type 3 fsr=%x expect section translation fault", f);
    CHECK((f & 0xf0u) == 0u, "fine-table rejection leaked obsolete domain: fsr=%x", f);
}

static void test_page_translation_fault_precedes_page_domain_fault(void) {
    arm_cpu_t c;
    uint32_t pa = 0;
    const uint32_t l1 = 0x4000u, l2 = 0x8000u, domain = 7u;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, l1 + (0x800u << 2), l2 | (domain << 5) | 1u);
    /* The selected L2 descriptor remains zero (translation fault) while its
     * domain is disabled. Translation has higher fault priority. */
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = l1;
    c.cp15.dacr = 0u;
    c.cp15.sctlr = ARM_SCTLR_M;
    uint32_t f = arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, true, &pa);
    CHECK((f & 0xfu) == ARM_FSR_PAGE_TRANSLATION,
          "invalid L2 plus disabled domain produced fsr=%x, expect page translation", f);

    /* Once the L2 descriptor is valid, the same disabled domain is reported. */
    m_w32(NULL, l2, 0x00030000u | (3u << 4) | 2u);
    /* The descriptor just changed under a cache that now exists. ARMv6
     * requires CP15 c8 maintenance after a table edit for exactly this
     * reason; a guest that skips it reads stale on hardware too. */
    arm_mmu_tlb_flush(&c);
    f = arm_mmu_translate(&c, 0x80000000u, ARM_ACCESS_READ, true, &pa);
    CHECK((f & 0xfu) == ARM_FSR_PAGE_DOMAIN,
          "valid L2 plus disabled domain produced fsr=%x, expect page domain", f);
}

static void test_force_access_flag_faults_precede_domain_permissions(void) {
    arm_cpu_t c;
    uint32_t pa, fsr;

    /* Extended section AP[0]=0 is an access-flag fault when SCTLR.FA is set.
     * Even a Manager domain cannot bypass it, and a store retains DFSR.WnR. */
    memset(g_ram, 0, sizeof g_ram);
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = 0x4000u;
    c.cp15.dacr = 3u << (5u * 2u);             /* domain 5: Manager */
    c.cp15.sctlr = ARM_SCTLR_M | ARM_SCTLR_XP | ARM_SCTLR_FA;
    m_w32(NULL, 0x4000u + (0x800u << 2),
          0x00200000u | (5u << 5) | 2u);       /* section, AP[0]=0 */
    g_watch_addr = 0x00200100u;
    g_watch_reads32 = g_watch_writes32 = 0u;
    pa = 0xdeadbeefu;
    fsr = arm_mmu_translate(&c, 0x80000100u, ARM_ACCESS_WRITE, true, &pa);
    CHECK((fsr & 0xfu) == ARM_FSR_SECTION_ACCESS_FLAG &&
          (fsr & 0xf0u) == 0x50u && (fsr & (1u << 11)) != 0u &&
          pa == 0xdeadbeefu,
          "section access-flag fsr=%x pa=%08x", fsr, pa);
    CHECK(g_watch_reads32 == 0u && g_watch_writes32 == 0u,
          "section access-flag fault touched target memory");

    /* ARM1176 preserves the deprecated APX:AP=000 S/R escape when S and R are
     * opposite. It remains a permission mapping, not an access-flag fault. */
    c.cp15.dacr = 1u << (5u * 2u);             /* domain 5: Client */
    c.cp15.sctlr = ARM_SCTLR_M | ARM_SCTLR_XP | ARM_SCTLR_FA | ARM_SCTLR_S;
    CHECK(arm_mmu_translate(&c, 0x80000100u, ARM_ACCESS_READ, true, &pa) == 0u,
          "S=1,R=0 privileged read was mistaken for an access-flag fault");
    fsr = arm_mmu_translate(&c, 0x80000100u, ARM_ACCESS_WRITE, true, &pa);
    CHECK((fsr & 0xfu) == ARM_FSR_SECTION_PERMISSION,
          "S/R compatibility write fsr=%x, expect permission fault", fsr);
    c.cp15.sctlr |= ARM_SCTLR_R;                /* S=1,R=1: reserved, no escape */
    fsr = arm_mmu_translate(&c, 0x80000100u, ARM_ACCESS_READ, true, &pa);
    CHECK((fsr & 0xfu) == ARM_FSR_SECTION_ACCESS_FLAG,
          "S=1,R=1 suppressed access-flag fault: fsr=%x", fsr);

    /* FA only changes the XP=1 format. With FA clear, or XP clear, the same
     * Manager mapping is allowed and AP[0] resumes its ordinary meaning. */
    c.cp15.dacr = 3u << (5u * 2u);
    c.cp15.sctlr = ARM_SCTLR_M | ARM_SCTLR_XP;
    CHECK(arm_mmu_translate(&c, 0x80000100u, ARM_ACCESS_READ, true, &pa) == 0u &&
          pa == 0x00200100u,
          "FA-clear Manager section did not translate: pa=%08x", pa);
    c.cp15.sctlr = ARM_SCTLR_M | ARM_SCTLR_FA; /* XP clear */
    CHECK(arm_mmu_translate(&c, 0x80000100u, ARM_ACCESS_READ, true, &pa) == 0u,
          "XP-clear descriptor incorrectly treated AP[0] as an access flag");

    /* A valid second-level descriptor is classified before its access flag;
     * once valid, page access-flag outranks a disabled domain. */
    memset(g_ram, 0, sizeof g_ram);
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = 0x4000u;
    c.cp15.dacr = 0u;                          /* every domain disabled */
    c.cp15.sctlr = ARM_SCTLR_M | ARM_SCTLR_XP | ARM_SCTLR_FA;
    m_w32(NULL, 0x4000u + (0x800u << 2), 0x8000u | (7u << 5) | 1u);
    pa = 0xdeadbeefu;
    fsr = arm_mmu_translate(&c, 0x80001000u, ARM_ACCESS_READ, true, &pa);
    CHECK((fsr & 0xfu) == ARM_FSR_PAGE_TRANSLATION,
          "invalid L2 with FA produced fsr=%x, expect page translation", fsr);

    m_w32(NULL, 0x8004u, 0x00060000u | 2u);    /* small page, AP[0]=0 */
    /* The descriptor just changed under a cache that now exists. ARMv6
     * requires CP15 c8 maintenance after a table edit for exactly this
     * reason; a guest that skips it reads stale on hardware too. */
    arm_mmu_tlb_flush(&c);
    g_watch_addr = 0x00060000u;
    g_watch_reads32 = g_watch_writes32 = 0u;
    pa = 0xdeadbeefu;
    fsr = arm_mmu_translate(&c, 0x80001000u, ARM_ACCESS_READ, true, &pa);
    CHECK((fsr & 0xfu) == ARM_FSR_PAGE_ACCESS_FLAG &&
          (fsr & 0xf0u) == 0x70u && pa == 0xdeadbeefu,
          "page access-flag fsr=%x pa=%08x", fsr, pa);
    CHECK(g_watch_reads32 == 0u && g_watch_writes32 == 0u,
          "page access-flag fault touched target memory");
    g_watch_addr = 0xffffffffu;
}

static void test_fetch_cache_refill_requires_an_exact_live_witness(void) {
    arm_bus_t bus = g_bus;
    arm_cpu_t c;
    uint32_t pa = 0u;
    uint64_t hits, misses, flushes;
    const uint32_t mapped_va = UINT32_C(0x80000420);

    bus.host_ram = m_host_ram;
    memset(g_ram, 0, sizeof g_ram);
    arm_reset(&c, &bus);

    /* MMU-off identity mapping needs no TLB entry and must not invent one in
     * the accounting. */
    hits = c.tlb_hits;
    CHECK(arm_fetch_cache_try_refill(&c, 0x420u, true) &&
          c.fetch_host == g_ram + 0x400u && c.fetch_blk == 0x400u &&
          c.fetch_gen == c.tlb_gen && c.fetch_priv &&
          c.tlb_hits == hits,
          "MMU-off fetch refill did not use the exact identity block");

    /* One privileged FETCH translation seeds the exact 1 KiB TLB witness. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x4000u + (0x800u << 2),
          (3u << 10) | 2u);                 /* VA 0x80000000 -> PA 0 */
    arm_reset(&c, &bus);
    c.cp15.ttbr0 = 0x4000u;
    c.cp15.dacr = 1u;
    c.cp15.sctlr = ARM_SCTLR_M;
    CHECK(arm_mmu_translate(&c, mapped_va, ARM_ACCESS_FETCH, true, &pa) == 0u &&
          pa == 0x420u,
          "could not seed fetch TLB witness: pa=%08x", pa);
    c.fetch_host = NULL;
    hits = c.tlb_hits;
    misses = c.tlb_misses;
    flushes = c.tlb_flushes;
    CHECK(arm_fetch_cache_try_refill(&c, mapped_va, true) &&
          c.fetch_host == g_ram + 0x400u &&
          c.fetch_blk == (mapped_va & ~UINT32_C(0x3ff)) &&
          c.fetch_gen == c.tlb_gen && c.fetch_priv,
          "exact TLB witness did not refill the host fetch block");
    CHECK(c.tlb_hits == hits + 1u && c.tlb_misses == misses &&
          c.tlb_flushes == flushes,
          "successful refill did not replace exactly one interpreter TLB hit");
    hits = c.tlb_hits;
    CHECK(arm_fetch_cache_try_refill(&c, mapped_va, true) &&
          c.tlb_hits == hits,
          "already-live fetch cache counted a second TLB hit");

    /* Privilege and register-stamp differences must refuse without changing
     * either the cache or the counters. */
    c.fetch_host = NULL;
    hits = c.tlb_hits;
    CHECK(!arm_fetch_cache_try_refill(&c, mapped_va, false) &&
          !c.fetch_host && c.tlb_hits == hits,
          "privileged FETCH witness was reused for an unprivileged fetch");
    c.cp15.context_id ^= 1u;
    CHECK(!arm_fetch_cache_try_refill(&c, mapped_va, true) &&
          !c.fetch_host && c.tlb_hits == hits,
          "stale MMU register stamp was accepted");

    /* Restore a current stamp, then distinguish a genuine miss, a cached
     * fault and a successful translation whose target is not plain RAM. */
    c.cp15.context_id ^= 1u;
    CHECK(arm_mmu_translate(&c, mapped_va, ARM_ACCESS_FETCH, true, &pa) == 0u,
          "could not restore the current MMU stamp");
    c.fetch_host = NULL;
    hits = c.tlb_hits;
    misses = c.tlb_misses;
    CHECK(!arm_fetch_cache_try_refill(&c, 0x80000820u, true) &&
          !c.fetch_host && c.tlb_hits == hits && c.tlb_misses == misses,
          "lookup-only refill walked or counted a missing TLB entry");

    pa = 0xdeadbeefu;
    CHECK(arm_mmu_translate(&c, 0x90000420u, ARM_ACCESS_FETCH, true, &pa) != 0u &&
          pa == 0xdeadbeefu,
          "could not seed a cached prefetch fault");
    hits = c.tlb_hits;
    CHECK(!arm_fetch_cache_try_refill(&c, 0x90000420u, true) &&
          !c.fetch_host && c.tlb_hits == hits,
          "cached prefetch fault was consumed or double-counted");

    m_w32(NULL, 0x4000u + (0x900u << 2),
          UINT32_C(0x00200000) | (3u << 10) | 2u);
    arm_mmu_tlb_flush(&c);
    CHECK(arm_mmu_translate(&c, 0x90000420u, ARM_ACCESS_FETCH, true, &pa) == 0u &&
          pa == 0x00200420u,
          "could not seed a non-RAM successful translation: pa=%08x", pa);
    hits = c.tlb_hits;
    CHECK(!arm_fetch_cache_try_refill(&c, 0x90000420u, true) &&
          !c.fetch_host && c.tlb_hits == hits,
          "non-RAM translation produced a direct fetch pointer");
}

static void test_abort_restores_base_register(void) {
    /* Base Restored Abort Model: after a data abort the base and destination
     * registers must be unchanged so the handler can retry the instruction. */
    arm_cpu_t c;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x4000 + 0, 0x00000000u | (3u << 10) | 2u);   /* identity map VA 0 */
    uint32_t prog[] = { 0xe4901004 };    /* LDR r1,[r0],#4  (post-indexed) */
    m_w32(NULL, 0, prog[0]);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.cp15.ttbr0 = 0x4000; c.cp15.dacr = 1u; c.cp15.sctlr |= ARM_SCTLR_M;
    c.r[0] = 0x00100000;                 /* unmapped */
    c.r[1] = 0x5a5a5a5a;
    arm_step(&c);
    CHECK(c.r[0] == 0x00100000u, "r0=%08x expect 00100000 (base restored)", c.r[0]);
    CHECK(c.r[1] == 0x5a5a5a5au, "r1=%08x expect 5a5a5a5a (dest untouched)", c.r[1]);
    CHECK(c.cp15.dfar == 0x00100000u, "dfar=%08x expect 00100000", c.cp15.dfar);
}

static void alignment_setup(arm_cpu_t *c, uint32_t insn) {
    g_watch_addr = 0xffffffffu;
    g_watch_reads32 = g_watch_writes32 = 0u;
    g_watch_reads16 = g_watch_writes16 = 0u;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, insn);
    arm_reset(c, &g_bus);
    c->cpsr = (c->cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c->r[15] = 0u;
}

static void alignment_map_normal_identity(arm_cpu_t *c) {
    const uint32_t l1 = 0x4000u;
    m_w32(NULL, l1, (3u << 10) | 8u | 2u);       /* Normal, full access */
    c->cp15.ttbr0 = l1;
    c->cp15.dacr = 1u;                            /* domain 0 Client */
    c->cp15.sctlr |= ARM_SCTLR_M | ARM_SCTLR_XP;
}

static void test_sctlr_a_faults_ordinary_unaligned_accesses(void) {
    arm_cpu_t c;
    alignment_setup(&c, 0xe5901000u);             /* LDR r1,[r0] */
    c.cp15.sctlr |= ARM_SCTLR_A | ARM_SCTLR_U;
    c.r[0] = 0x801u;
    c.r[1] = 0xdeadbeefu;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "unaligned LDR with A=1 did not take a data abort");
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT && c.cp15.dfar == 0x801u,
          "LDR alignment state dfsr=%x dfar=%08x", c.cp15.dfsr, c.cp15.dfar);
    CHECK((c.cp15.dfsr & (1u << 11)) == 0u && c.r[1] == 0xdeadbeefu,
          "faulting LDR changed WnR or destination");
    CHECK(g_watch_reads32 == 0u, "alignment-faulting LDR issued %u bus reads",
          g_watch_reads32);

    alignment_setup(&c, 0xe5801000u);             /* STR r1,[r0] */
    c.cp15.sctlr |= ARM_SCTLR_A | ARM_SCTLR_U;
    c.r[0] = 0x801u;
    c.r[1] = 0x11223344u;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "unaligned STR with A=1 did not take a data abort");
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT &&
          (c.cp15.dfsr & (1u << 11)) != 0u && c.cp15.dfar == 0x801u,
          "STR alignment state dfsr=%x dfar=%08x", c.cp15.dfsr, c.cp15.dfar);
    CHECK(g_watch_writes32 == 0u, "alignment-faulting STR issued %u bus writes",
          g_watch_writes32);
    g_watch_addr = 0xffffffffu;
}

static void test_sctlr_u_selects_legacy_or_armv6_unaligned_data(void) {
    arm_cpu_t c;

    /* Legacy U=0 word loads align down, then rotate right by the byte offset. */
    alignment_setup(&c, 0xe5901000u);             /* LDR r1,[r0] */
    m_w32(NULL, 0x800u, 0x11223344u);
    c.cp15.sctlr &= ~(ARM_SCTLR_A | ARM_SCTLR_U);
    c.r[0] = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[1] == 0x44112233u,
          "legacy unaligned LDR=%08x expect rotate-right 44112233", c.r[1]);

    /* Legacy word stores align down without rotation. */
    alignment_setup(&c, 0xe5801000u);             /* STR r1,[r0] */
    c.cp15.sctlr &= ~(ARM_SCTLR_A | ARM_SCTLR_U);
    c.r[0] = 0x803u;
    c.r[1] = 0xa1b2c3d4u;
    g_watch_addr = 0x800u;
    CHECK(arm_step(&c) == ARM_OK && m_r32(NULL, 0x800u) == 0xa1b2c3d4u,
          "legacy unaligned STR did not align down");
    CHECK(g_watch_writes32 == 1u, "legacy STR issued %u aligned writes expect 1",
          g_watch_writes32);

    /* Unlike words, an odd U=0/A=0 halfword is architecturally
     * UNPREDICTABLE. Refuse it before the align-down bus behavior leaks out as
     * a fabricated value. */
    alignment_setup(&c, 0xe1d010b0u);             /* LDRH r1,[r0] */
    m_w16(NULL, 0x800u, 0x1234u);
    c.cp15.sctlr &= ~(ARM_SCTLR_A | ARM_SCTLR_U);
    c.r[0] = 0x801u;
    c.r[1] = 0xdeadbeefu;
    g_watch_addr = 0x800u;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[1] == 0xdeadbeefu,
          "legacy odd LDRH should be refused without changing r1");
    CHECK(g_watch_reads16 == 0u, "undefined legacy LDRH issued %u bus reads",
          g_watch_reads16);

    /* U=1 enables the bytewise ARMv6 value in Normal memory. */
    alignment_setup(&c, 0xe5901000u);
    alignment_map_normal_identity(&c);
    g_ram[0x801u] = 0x11u; g_ram[0x802u] = 0x22u;
    g_ram[0x803u] = 0x33u; g_ram[0x804u] = 0x44u;
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[0] = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[1] == 0x44332211u,
          "ARMv6 U=1 LDR=%08x expect unaligned 44332211", c.r[1]);

    alignment_setup(&c, 0xe1d010b0u);
    alignment_map_normal_identity(&c);
    g_ram[0x801u] = 0x34u; g_ram[0x802u] = 0x12u;
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[0] = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[1] == 0x1234u,
          "ARMv6 U=1 LDRH=%08x expect unaligned 1234", c.r[1]);
    g_watch_addr = 0xffffffffu;
}

static void test_multiword_alignment_depends_on_sctlr_u_a(void) {
    arm_cpu_t c;

    /* U=1 makes an unaligned LDM an alignment fault even though A is clear. */
    alignment_setup(&c, 0xe8900006u);             /* LDMIA r0,{r1,r2} */
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[0] = 0x801u;
    c.r[1] = 0xaaaaaaaau; c.r[2] = 0xbbbbbbbbu;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "U=1 misaligned LDM did not take an alignment abort");
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT &&
          (c.cp15.dfsr & (1u << 11)) == 0u,
          "LDM alignment dfsr=%x", c.cp15.dfsr);
    CHECK(g_watch_reads32 == 0u && c.r[1] == 0xaaaaaaaau && c.r[2] == 0xbbbbbbbbu,
          "faulting LDM touched the bus or destination registers");

    /* U=0/A=0 keeps the legacy align-down behavior for a multiple transfer. */
    alignment_setup(&c, 0xe8900006u);
    m_w32(NULL, 0x800u, 0x11223344u);
    m_w32(NULL, 0x804u, 0x55667788u);
    c.cp15.sctlr &= ~(ARM_SCTLR_A | ARM_SCTLR_U);
    c.r[0] = 0x803u;
    CHECK(arm_step(&c) == ARM_OK && c.r[1] == 0x11223344u &&
          c.r[2] == 0x55667788u,
          "legacy LDM did not align start down: %08x/%08x", c.r[1], c.r[2]);

    /* The write form reports WnR and performs no first transaction. */
    alignment_setup(&c, 0xe8800006u);             /* STMIA r0,{r1,r2} */
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[0] = 0x801u;
    c.r[1] = 0x11223344u; c.r[2] = 0x55667788u;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "U=1 misaligned STM did not take an alignment abort");
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT &&
          (c.cp15.dfsr & (1u << 11)) != 0u && g_watch_writes32 == 0u,
          "STM alignment dfsr=%x writes=%u", c.cp15.dfsr, g_watch_writes32);

    /* Coprocessor transfers use the same strict U=1 word-alignment rule. */
    alignment_setup(&c, 0xed910a00u);             /* VLDR s0,[r1] */
    c.cp15.cpacr = 0xfu << 20;
    c.vfp_fpexc = ARM_FPEXC_EN;
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[1] = 0x801u;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT &&
          (c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT,
          "U=1 misaligned VLDR did not take an alignment abort");
    CHECK(g_watch_reads32 == 0u, "misaligned VLDR issued %u bus reads",
          g_watch_reads32);
    g_watch_addr = 0xffffffffu;
}

static void test_arm_multiword_base_writeback_restrictions(void) {
    arm_cpu_t c;

    alignment_setup(&c, 0xe8b10002u);           /* LDMIA r1!,{r1} */
    c.r[1] = 0x800u;
    g_watch_addr = 0x800u;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[1] == 0x800u,
          "LDM writeback with its base in the list must be UNPREDICTABLE");
    CHECK(g_watch_reads32 == 0u,
          "undefined base-in-list LDM issued %u reads", g_watch_reads32);

    alignment_setup(&c, 0xe8a10003u);           /* STMIA r1!,{r0,r1} */
    c.r[0] = 0x11223344u;
    c.r[1] = 0x800u;
    g_watch_addr = 0x800u;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[1] == 0x800u,
          "STM with a non-lowest writeback base must be UNPREDICTABLE");
    CHECK(g_watch_writes32 == 0u,
          "undefined non-lowest-base STM issued %u writes", g_watch_writes32);

    /* Rn in the list is defined for STM when it is the lowest-numbered
     * register: the original base is stored before normal writeback. */
    alignment_setup(&c, 0xe8a00003u);           /* STMIA r0!,{r0,r1} */
    c.r[0] = 0x800u;
    c.r[1] = 0xa1b2c3d4u;
    CHECK(arm_step(&c) == ARM_OK && m_r32(NULL, 0x800u) == 0x800u &&
          m_r32(NULL, 0x804u) == 0xa1b2c3d4u && c.r[0] == 0x808u,
          "defined lowest-base STM did not store the original base/write back");
    g_watch_addr = 0xffffffffu;
}

static void check_unpredictable_single_transfer(uint32_t insn,
                                                uint32_t target,
                                                const char *what) {
    arm_cpu_t c;
    uint32_t sentinel = 0x5a96c33cu;

    g_watch_addr = 0xffffffffu;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, insn);
    m_w32(NULL, target, sentinel);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.r[0] = 0x800u;
    c.r[1] = 0x800u;
    c.r[2] = 0x10u;
    c.r[15] = 0u;

    g_watch_addr = target;
    g_watch_reads8 = g_watch_writes8 = 0u;
    g_watch_reads16 = g_watch_writes16 = 0u;
    g_watch_reads32 = g_watch_writes32 = 0u;
    arm_status_t st = arm_step(&c);
    g_watch_addr = 0xffffffffu;

    uint32_t after;
    memcpy(&after, &g_ram[target & (RAM_SIZE - 1u)], sizeof after);
    CHECK(st == ARM_UNDEFINED, "%s returned status %d", what, (int)st);
    CHECK(g_watch_reads8 == 0u && g_watch_writes8 == 0u &&
          g_watch_reads16 == 0u && g_watch_writes16 == 0u &&
          g_watch_reads32 == 0u && g_watch_writes32 == 0u,
          "%s touched its target bus address (r8=%u w8=%u r16=%u w16=%u "
          "r32=%u w32=%u)", what, g_watch_reads8, g_watch_writes8,
          g_watch_reads16, g_watch_writes16, g_watch_reads32, g_watch_writes32);
    CHECK(c.r[0] == 0x800u && c.r[1] == 0x800u && c.r[2] == 0x10u &&
          c.r[15] == 0u && after == sentinel,
          "%s changed architectural state before trapping", what);
}

static void test_single_transfer_zero_post_same_base_load(void) {
    arm_cpu_t c;

    g_watch_addr = 0xffffffffu;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe4933000u); /* LDR r3,[r3],#0 */
    m_w32(NULL, 0x800u, 0x12345678u);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.r[3] = 0x800u;
    c.r[15] = 0u;

    g_watch_addr = 0x800u;
    g_watch_reads8 = g_watch_writes8 = 0u;
    g_watch_reads16 = g_watch_writes16 = 0u;
    g_watch_reads32 = g_watch_writes32 = 0u;
    arm_status_t st = arm_step(&c);
    g_watch_addr = 0xffffffffu;

    CHECK(st == ARM_OK && c.r[3] == 0x12345678u && c.r[15] == 4u,
          "zero-offset LDR Rd,[Rd],#0 status=%d rd=%08x pc=%08x",
          (int)st, c.r[3], c.r[15]);
    CHECK(g_watch_reads32 == 1u && g_watch_writes32 == 0u &&
          g_watch_reads8 == 0u && g_watch_writes8 == 0u &&
          g_watch_reads16 == 0u && g_watch_writes16 == 0u,
          "zero-offset LDR Rd,[Rd],#0 bus r8=%u w8=%u r16=%u w16=%u "
          "r32=%u w32=%u",
          g_watch_reads8, g_watch_writes8,
          g_watch_reads16, g_watch_writes16,
          g_watch_reads32, g_watch_writes32);
}

static void test_single_transfer_unpredictable_forms_trap_before_bus(void) {
    /* Addressing mode 2: any post-indexed form writes back, as does P=1,W=1.
     * Apart from the separately tested immediate-zero load, Rn==Rd is
     * ambiguous for both loads and stores. R15 cannot be a writeback base or
     * a register offset, and byte transfers reserve R15 as their data
     * register. */
    check_unpredictable_single_transfer(0xe4900004u, 0x800u,
                                        "LDR r0,[r0],#4");
    check_unpredictable_single_transfer(0xe4800004u, 0x800u,
                                        "STR r0,[r0],#4");
    check_unpredictable_single_transfer(0xe5b00004u, 0x804u,
                                        "LDR r0,[r0,#4]!");
    check_unpredictable_single_transfer(0xe5a00004u, 0x804u,
                                        "STR r0,[r0,#4]!");
    check_unpredictable_single_transfer(0xe49f0004u, 0x008u,
                                        "LDR r0,[pc],#4");
    check_unpredictable_single_transfer(0xe791000fu, 0x808u,
                                        "LDR r0,[r1,pc]");
    check_unpredictable_single_transfer(0xe5d1f000u, 0x800u,
                                        "LDRB pc,[r1]");
    check_unpredictable_single_transfer(0xe5c1f000u, 0x800u,
                                        "STRB pc,[r1]");
    check_unpredictable_single_transfer(0xe4b1f004u, 0x800u,
                                        "LDRT pc,[r1],#4");

    /* Addressing mode 3 has no P=0,W=1 translation form. Its register-offset
     * encoding reserves bits[11:8] and R15, and its writeback aliases have the
     * same ambiguity as mode 2. */
    check_unpredictable_single_transfer(0xe0f100b2u, 0x800u,
                                        "invalid P=0,W=1 LDRH");
    check_unpredictable_single_transfer(0xe19101b2u, 0x810u,
                                        "LDRH with nonzero SBZ bits");
    check_unpredictable_single_transfer(0xe19100bfu, 0x808u,
                                        "LDRH r0,[r1,pc]");
    check_unpredictable_single_transfer(0xe1f000b2u, 0x802u,
                                        "LDRH r0,[r0,#2]!");
    check_unpredictable_single_transfer(0xe0c000b2u, 0x800u,
                                        "STRH r0,[r0],#2");
    check_unpredictable_single_transfer(0xe1ff00b2u, 0x00au,
                                        "LDRH r0,[pc,#2]!");
}

/*
 * LDRD/STRD. These are ARM-mode only, which is why a Thumb-compiled daemon
 * never reached them, but ARM library code does. The pair is Rd and Rd+1 and
 * the transfer is two ordinary word accesses at addr and addr+4, so everything
 * about the addressing modes is shared with the halfword forms above.
 */
static void test_ldrd_strd_transfer_the_register_pair(void) {
    arm_cpu_t c;

    /* LDRD r0,r1,[r2]: low word into Rd, high word into Rd+1. */
    alignment_setup(&c, 0xe1c200d0u);             /* LDRD r0,r1,[r2] */
    m_w32(NULL, 0x800u, 0x11223344u);
    m_w32(NULL, 0x804u, 0x55667788u);
    c.cp15.sctlr |= ARM_SCTLR_U;
    c.r[2] = 0x800u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == 4u &&
          c.r[0] == 0x11223344u && c.r[1] == 0x55667788u,
          "LDRD pair=%08x/%08x expect 11223344/55667788", c.r[0], c.r[1]);

    /* STRD r0,r1,[r2] writes BOTH words, in ascending address order. */
    alignment_setup(&c, 0xe1c200f0u);             /* STRD r0,r1,[r2] */
    c.cp15.sctlr |= ARM_SCTLR_U;
    c.r[0] = 0xa1b2c3d4u; c.r[1] = 0xe5f60718u; c.r[2] = 0x800u;
    CHECK(arm_step(&c) == ARM_OK && m_r32(NULL, 0x800u) == 0xa1b2c3d4u &&
          m_r32(NULL, 0x804u) == 0xe5f60718u,
          "STRD wrote %08x/%08x expect a1b2c3d4/e5f60718",
          m_r32(NULL, 0x800u), m_r32(NULL, 0x804u));

    /* The addressing-mode-3 machinery is the shared one: immediate offset up
     * and down, register offset, and both writeback forms. */
    alignment_setup(&c, 0xe1c200d8u);             /* LDRD r0,r1,[r2,#8] */
    m_w32(NULL, 0x808u, 0xdeadbeefu);
    m_w32(NULL, 0x80cu, 0xfeedfaceu);
    c.cp15.sctlr |= ARM_SCTLR_U;
    c.r[2] = 0x800u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0xdeadbeefu &&
          c.r[1] == 0xfeedfaceu && c.r[2] == 0x800u,
          "LDRD [r2,#8] pair=%08x/%08x base=%08x", c.r[0], c.r[1], c.r[2]);

    alignment_setup(&c, 0xe14200d8u);             /* LDRD r0,r1,[r2,#-8] */
    m_w32(NULL, 0x800u, 0x13579bdfu);
    m_w32(NULL, 0x804u, 0x02468aceu);
    c.cp15.sctlr |= ARM_SCTLR_U;
    c.r[2] = 0x808u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0x13579bdfu &&
          c.r[1] == 0x02468aceu,
          "LDRD [r2,#-8] pair=%08x/%08x", c.r[0], c.r[1]);

    alignment_setup(&c, 0xe18200d3u);             /* LDRD r0,r1,[r2,r3] */
    m_w32(NULL, 0x810u, 0x0000cafeu);
    m_w32(NULL, 0x814u, 0x0000babeu);
    c.cp15.sctlr |= ARM_SCTLR_U;
    c.r[2] = 0x800u; c.r[3] = 0x10u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0x0000cafeu &&
          c.r[1] == 0x0000babeu,
          "LDRD [r2,r3] pair=%08x/%08x", c.r[0], c.r[1]);

    alignment_setup(&c, 0xe0c200d8u);             /* LDRD r0,r1,[r2],#8 */
    m_w32(NULL, 0x800u, 0x00000001u);
    m_w32(NULL, 0x804u, 0x00000002u);
    c.cp15.sctlr |= ARM_SCTLR_U;
    c.r[2] = 0x800u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 1u && c.r[1] == 2u &&
          c.r[2] == 0x808u,
          "post-indexed LDRD pair=%08x/%08x base=%08x", c.r[0], c.r[1], c.r[2]);

    alignment_setup(&c, 0xe1e200f8u);             /* STRD r0,r1,[r2,#8]! */
    c.cp15.sctlr |= ARM_SCTLR_U;
    c.r[0] = 0x0f0f0f0fu; c.r[1] = 0xf0f0f0f0u; c.r[2] = 0x800u;
    CHECK(arm_step(&c) == ARM_OK && m_r32(NULL, 0x808u) == 0x0f0f0f0fu &&
          m_r32(NULL, 0x80cu) == 0xf0f0f0f0u && c.r[2] == 0x808u,
          "pre-indexed STRD wrote %08x/%08x base=%08x",
          m_r32(NULL, 0x808u), m_r32(NULL, 0x80cu), c.r[2]);
}

static void test_ldrd_strd_alignment_is_the_armv6_word_rule(void) {
    arm_cpu_t c;

    /*
     * The case most likely to be got wrong. ARMv5TE required a doubleword-
     * aligned address; ARMv6 relaxed that to the multiword rule, which is word
     * granularity. base+4 is word aligned but NOT doubleword aligned, and with
     * SCTLR.U=1, A=0 it must succeed rather than fault.
     */
    alignment_setup(&c, 0xe1c200d0u);             /* LDRD r0,r1,[r2] */
    m_w32(NULL, 0x804u, 0x0badf00du);
    m_w32(NULL, 0x808u, 0x8badbeefu);
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[2] = 0x804u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == 4u &&
          c.r[0] == 0x0badf00du && c.r[1] == 0x8badbeefu,
          "word-but-not-doubleword-aligned LDRD pair=%08x/%08x pc=%08x",
          c.r[0], c.r[1], c.r[15]);

    alignment_setup(&c, 0xe1c200f0u);             /* STRD r0,r1,[r2] */
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[0] = 0x12345678u; c.r[1] = 0x9abcdef0u; c.r[2] = 0x804u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == 4u &&
          m_r32(NULL, 0x804u) == 0x12345678u &&
          m_r32(NULL, 0x808u) == 0x9abcdef0u,
          "word-but-not-doubleword-aligned STRD wrote %08x/%08x pc=%08x",
          m_r32(NULL, 0x804u), m_r32(NULL, 0x808u), c.r[15]);

    /* A is the alignment CHECK, not a stricter alignment: a word-aligned pair
     * is still legal with A set, exactly as it is for LDM. */
    alignment_setup(&c, 0xe1c200d0u);
    m_w32(NULL, 0x804u, 0xaaaa5555u);
    m_w32(NULL, 0x808u, 0x5555aaaau);
    c.cp15.sctlr |= ARM_SCTLR_A | ARM_SCTLR_U;
    c.r[2] = 0x804u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0xaaaa5555u &&
          c.r[1] == 0x5555aaaau,
          "A=1 word-aligned LDRD pair=%08x/%08x", c.r[0], c.r[1]);

    /* base+2 is not word aligned: alignment abort, and no bus access at all. */
    alignment_setup(&c, 0xe1c200d0u);
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[0] = 0xdeadbeefu; c.r[1] = 0xfeedfaceu; c.r[2] = 0x802u;
    g_watch_addr = 0x802u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "halfword-aligned LDRD did not take an alignment abort");
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT &&
          (c.cp15.dfsr & (1u << 11)) == 0u && c.cp15.dfar == 0x802u,
          "LDRD alignment state dfsr=%x dfar=%08x", c.cp15.dfsr, c.cp15.dfar);
    CHECK(g_watch_reads32 == 0u && c.r[0] == 0xdeadbeefu &&
          c.r[1] == 0xfeedfaceu,
          "alignment-faulting LDRD issued %u reads or moved the pair",
          g_watch_reads32);

    /* The store form reports WnR and performs no transaction either. */
    alignment_setup(&c, 0xe1c200f0u);
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[0] = 0x11223344u; c.r[1] = 0x55667788u; c.r[2] = 0x802u;
    g_watch_addr = 0x802u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "halfword-aligned STRD did not take an alignment abort");
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT &&
          (c.cp15.dfsr & (1u << 11)) != 0u && c.cp15.dfar == 0x802u &&
          g_watch_writes32 == 0u,
          "STRD alignment dfsr=%x dfar=%08x writes=%u",
          c.cp15.dfsr, c.cp15.dfar, g_watch_writes32);

    /* Legacy U=0/A=0 aligns the transfer down, like any other multiword one —
     * and the aligned-down value must not leak into the writeback, which is
     * still base+offset. */
    alignment_setup(&c, 0xe1e200d3u);             /* LDRD r0,r1,[r2,#3]! */
    m_w32(NULL, 0x800u, 0x11223344u);
    m_w32(NULL, 0x804u, 0x55667788u);
    c.cp15.sctlr &= ~(ARM_SCTLR_A | ARM_SCTLR_U);
    c.r[2] = 0x800u;
    CHECK(arm_step(&c) == ARM_OK && c.r[0] == 0x11223344u &&
          c.r[1] == 0x55667788u,
          "legacy LDRD did not align the transfer down: %08x/%08x",
          c.r[0], c.r[1]);
    CHECK(c.r[2] == 0x803u,
          "base=%08x expect 803 — writeback is base+offset, not the "
          "aligned-down transfer address", c.r[2]);
    g_watch_addr = 0xffffffffu;
}

static void test_ldrd_strd_operand_restrictions_trap_before_bus(void) {
    /* Rd names a PAIR: it must be even, and R14 would make R15 its second
     * half. A writeback base may alias neither half. Every one is
     * UNPREDICTABLE and must be refused before any bus access. */
    check_unpredictable_single_transfer(0xe1c130d0u, 0x800u,
                                        "LDRD r3,r4,[r1] (odd Rd)");
    check_unpredictable_single_transfer(0xe1c130f0u, 0x800u,
                                        "STRD r3,r4,[r1] (odd Rd)");
    check_unpredictable_single_transfer(0xe1c1e0d0u, 0x800u,
                                        "LDRD r14,pc,[r1]");
    check_unpredictable_single_transfer(0xe1c1e0f0u, 0x800u,
                                        "STRD r14,pc,[r1]");
    check_unpredictable_single_transfer(0xe1c1f0d0u, 0x800u,
                                        "LDRD pc,[r1]");
    check_unpredictable_single_transfer(0xe1e100d4u, 0x804u,
                                        "LDRD r0,r1,[r1,#4]! (Rn == Rd+1)");
    check_unpredictable_single_transfer(0xe0c100f8u, 0x800u,
                                        "STRD r0,r1,[r1],#8 (Rn == Rd+1)");
    check_unpredictable_single_transfer(0xe0c000d8u, 0x800u,
                                        "LDRD r0,r1,[r0],#8 (Rn == Rd)");
    check_unpredictable_single_transfer(0xe0e100d4u, 0x800u,
                                        "LDRD with the invalid P=0,W=1 form");
    check_unpredictable_single_transfer(0xe18101d2u, 0x810u,
                                        "LDRD with nonzero SBZ bits");
}

static void test_exclusive_alignment_is_never_silently_fixed(void) {
    arm_cpu_t c;
    alignment_setup(&c, 0xe1901f9fu);             /* LDREX r1,[r0] */
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[0] = 0x801u;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT &&
          (c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT,
          "U=1 misaligned LDREX did not alignment-fault");
    CHECK(g_watch_reads32 == 0u && !c.excl_valid,
          "misaligned LDREX touched the bus or set the monitor");

    alignment_setup(&c, 0xe1b14f9fu);             /* LDREXD r4,r5,[r1] */
    c.cp15.sctlr = (c.cp15.sctlr & ~ARM_SCTLR_A) | ARM_SCTLR_U;
    c.r[1] = 0x804u;                              /* word, not doubleword aligned */
    g_watch_addr = 0x804u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT &&
          (c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT,
          "word-aligned but not 64-bit-aligned LDREXD did not fault");
    CHECK(g_watch_reads32 == 0u && !c.excl_valid,
          "misaligned LDREXD touched the bus or set the monitor");

    /* In the legacy 00 configuration this form is UNPREDICTABLE, not an
     * ordinary word load that can be aligned down and rotated. */
    alignment_setup(&c, 0xe1901f9fu);
    c.cp15.sctlr &= ~(ARM_SCTLR_A | ARM_SCTLR_U);
    c.r[0] = 0x801u;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_UNDEFINED,
          "legacy misaligned LDREX should be refused as UNPREDICTABLE");
    CHECK(g_watch_reads32 == 0u && !c.excl_valid,
          "undefined LDREX touched the bus or set the monitor");
    g_watch_addr = 0xffffffffu;
}

static void check_unpredictable_atomic_operands(uint32_t insn, const char *what) {
    arm_cpu_t c;
    uint32_t before[16];

    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, insn);
    m_w32(NULL, 0x800u, 0x12345678u);
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    for (unsigned i = 0; i < 15u; i++) c.r[i] = 0x10000000u + i;
    c.r[1] = 0x800u;                         /* base for every case */
    c.r[15] = 0u;
    c.excl_valid = true;
    c.excl_addr = 0x800u;
    memcpy(before, c.r, sizeof before);

    g_watch_addr = 0x800u;
    g_watch_reads8 = g_watch_writes8 = 0u;
    g_watch_reads16 = g_watch_writes16 = 0u;
    g_watch_reads32 = g_watch_writes32 = 0u;
    arm_status_t st = arm_step(&c);
    g_watch_addr = 0xffffffffu;

    CHECK(st == ARM_UNDEFINED, "%s returned status %d", what, (int)st);
    CHECK(memcmp(before, c.r, sizeof before) == 0 &&
          c.excl_valid && c.excl_addr == 0x800u,
          "%s changed registers or consumed the monitor", what);
    CHECK(g_watch_reads8 == 0u && g_watch_writes8 == 0u &&
          g_watch_reads16 == 0u && g_watch_writes16 == 0u &&
          g_watch_reads32 == 0u && g_watch_writes32 == 0u,
          "%s touched memory before operand validation", what);
}

static void test_atomic_operand_aliases_are_rejected_before_bus(void) {
    check_unpredictable_atomic_operands(0xe1811f90u, "STREX status==base");
    check_unpredictable_atomic_operands(0xe1810f90u, "STREX status==data");
    check_unpredictable_atomic_operands(0xe1a11f96u, "STREXD status==base");
    check_unpredictable_atomic_operands(0xe1a16f96u, "STREXD status==data-low");
    check_unpredictable_atomic_operands(0xe1a17f96u, "STREXD status==data-high");
    check_unpredictable_atomic_operands(0xe1c11f90u, "STREXB status==base");
    check_unpredictable_atomic_operands(0xe1c10f90u, "STREXB status==data");
    check_unpredictable_atomic_operands(0xe1e11f90u, "STREXH status==base");
    check_unpredictable_atomic_operands(0xe1e10f90u, "STREXH status==data");
    check_unpredictable_atomic_operands(0xe1012091u, "SWP base==data");
    check_unpredictable_atomic_operands(0xe1011090u, "SWP base==destination");
}

/* --------------------------------------------------------------- Thumb --- */

/* Load 16-bit Thumb halfwords at `base` and run from there in Thumb state. */
static void load_thumb(arm_cpu_t *c, const uint16_t *prog, size_t n, int steps) {
    memset(g_ram, 0, sizeof g_ram);
    for (size_t i = 0; i < n; i++) m_w16(NULL, (uint32_t)(i*2), prog[i]);
    arm_reset(c, &g_bus);
    c->cpsr = (c->cpsr & ~0x1fu) | ARM_MODE_SYS;
    c->cpsr |= ARM_CPSR_T;
    c->r[15] = 0;
    for (int i = 0; i < steps; i++) if (arm_step(c) != ARM_OK) break;
}

static void test_thumb_mov_add(void) {
    /* MOV r0,#40 ; MOV r1,#2 ; ADD r0,r0,r1 */
    uint16_t p[] = { 0x2028, 0x2102, 0x1840 };
    arm_cpu_t c; load_thumb(&c, p, 3, 3);
    CHECK(c.r[0] == 42, "r0=%u expect 42", c.r[0]);
    CHECK(c.r[15] == 6, "pc=%u expect 6 (2 bytes per instruction)", c.r[15]);
}

static void test_thumb_lsl_flags(void) {
    /* MOV r0,#1 ; LSL r1,r0,#31 -> N set */
    uint16_t p[] = { 0x2001, 0x07c1 };
    arm_cpu_t c; load_thumb(&c, p, 2, 2);
    CHECK(c.r[1] == 0x80000000u, "r1=%08x expect 80000000", c.r[1]);
    CHECK((c.cpsr & ARM_CPSR_N) != 0, "N should be set");
}

static void test_thumb_push_pop(void) {
    /* SP=0x900 ; r0=0xAA ; r1=0xBB ; PUSH {r0,r1} ; POP {r2,r3} */
    uint16_t p[] = { 0x20aa,        /* MOV r0,#0xAA */
                     0x21bb,        /* MOV r1,#0xBB */
                     0xb403,        /* PUSH {r0,r1} */
                     0xbc0c };      /* POP  {r2,r3} */
    arm_cpu_t c; load_thumb(&c, p, 4, 0);
    c.r[13] = 0x900;
    for (int i = 0; i < 4; i++) arm_step(&c);
    CHECK(c.r[2] == 0xaa, "r2=%08x expect aa", c.r[2]);
    CHECK(c.r[3] == 0xbb, "r3=%08x expect bb", c.r[3]);
    CHECK(c.r[13] == 0x900, "sp=%08x expect 900 (balanced)", c.r[13]);
}

static void thumb_alignment_setup(arm_cpu_t *c, uint16_t insn) {
    g_watch_addr = 0xffffffffu;
    g_watch_reads32 = g_watch_writes32 = 0u;
    memset(g_ram, 0, sizeof g_ram);
    m_w16(NULL, 0u, insn);
    arm_reset(c, &g_bus);
    c->cpsr = (c->cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS | ARM_CPSR_T;
    c->r[15] = 0u;
}

static void test_thumb_multiword_alignment_uses_strict_rules(void) {
    arm_cpu_t c;

    /* Thumb LDMIA is a multiple transfer too: U=1 makes a misaligned base an
     * alignment abort before the first read, rather than an ordinary unaligned
     * LDR performed once per register. */
    thumb_alignment_setup(&c, 0xc802u);           /* LDMIA r0!,{r1} */
    c.cp15.sctlr = ARM_SCTLR_U;
    c.r[0] = 0x801u;
    c.r[1] = 0xdeadbeefu;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT &&
          (c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT,
          "misaligned Thumb LDMIA did not take an alignment abort");
    CHECK(g_watch_reads32 == 0u && c.r[0] == 0x801u &&
          c.r[1] == 0xdeadbeefu,
          "faulting Thumb LDMIA touched the bus, base, or destination");

    /* PUSH has a separate Thumb decoder path and must apply the same rule.
     * Its fault is a write and the pre-decremented first address is reported. */
    thumb_alignment_setup(&c, 0xb402u);           /* PUSH {r1} */
    c.cp15.sctlr = ARM_SCTLR_U;
    c.r[13] = 0x805u;
    c.r[1] = 0x11223344u;
    g_watch_addr = 0x801u;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT &&
          (c.cp15.dfsr & 0xfu) == ARM_FSR_ALIGNMENT &&
          (c.cp15.dfsr & (1u << 11)) != 0u && c.cp15.dfar == 0x801u,
          "misaligned Thumb PUSH did not report a write alignment abort");
    CHECK(g_watch_writes32 == 0u && c.bank_r13[ARM_BANK_USR] == 0x805u,
          "faulting Thumb PUSH touched memory or changed SP");

    /* Legacy U=0/A=0 aligns the memory address down but computes writeback
     * from the original base, just like the ARM-state multiple form. */
    thumb_alignment_setup(&c, 0xc802u);           /* LDMIA r0!,{r1} */
    m_w32(NULL, 0x800u, 0xa1b2c3d4u);
    c.r[0] = 0x803u;
    g_watch_addr = 0x800u;
    CHECK(arm_step(&c) == ARM_OK && c.r[1] == 0xa1b2c3d4u &&
          c.r[0] == 0x807u && g_watch_reads32 == 1u,
          "legacy Thumb LDMIA did not align data and preserve writeback");

    /* Empty PUSH/POP lists are UNPREDICTABLE; refuse them without touching
     * the stack instead of treating them as benign no-ops. */
    thumb_alignment_setup(&c, 0xb400u);           /* PUSH {} */
    c.r[13] = 0x900u;
    g_watch_addr = 0x8fcu;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[13] == 0x900u &&
          g_watch_writes32 == 0u,
          "empty Thumb PUSH was not refused before memory/writeback");

    /* Like ARM STM, Thumb STMIA only defines a base-in-list store when the
     * base is the lowest-numbered register. */
    thumb_alignment_setup(&c, 0xc103u);           /* STMIA r1!,{r0,r1} */
    c.r[0] = 0x11223344u;
    c.r[1] = 0x800u;
    g_watch_addr = 0x800u;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[1] == 0x800u &&
          g_watch_writes32 == 0u,
          "Thumb STMIA accepted a non-lowest writeback base");

    thumb_alignment_setup(&c, 0xc003u);           /* STMIA r0!,{r0,r1} */
    c.r[0] = 0x800u;
    c.r[1] = 0x55667788u;
    CHECK(arm_step(&c) == ARM_OK && m_r32(NULL, 0x800u) == 0x800u &&
          m_r32(NULL, 0x804u) == 0x55667788u && c.r[0] == 0x808u,
          "defined Thumb lowest-base STMIA stored/writeback incorrectly");

    thumb_alignment_setup(&c, 0xbd00u);           /* POP {pc} */
    m_w32(NULL, 0x800u, 0x102u);
    c.r[13] = 0x800u;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[15] == 0u &&
          c.r[13] == 0x800u && (c.cpsr & ARM_CPSR_T) != 0u,
          "Thumb POP accepted an impossible ARM-state 0b10 target");
    g_watch_addr = 0xffffffffu;
}

static void test_thumb_conditional_branch(void) {
    /* MOV r0,#1 ; CMP r0,#1 ; BNE +4 (not taken) ; MOV r1,#7 */
    uint16_t p[] = { 0x2001, 0x2801, 0xd101, 0x2107 };
    arm_cpu_t c; load_thumb(&c, p, 4, 4);
    CHECK(c.r[1] == 7, "r1=%u expect 7 (BNE not taken)", c.r[1]);
}

static void test_arm_to_thumb_and_back(void) {
    /* ARM: set r0 = 0x10|1 and BX to it, landing in Thumb at 0x10.
     * Thumb at 0x10: MOV r1,#5 ; then BX r2 with r2 = 0x20 (back to ARM).
     * ARM at 0x20: MOV r3,#9. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x00, 0xe3a00011);      /* MOV r0,#0x11        */
    m_w32(NULL, 0x04, 0xe3a02020);      /* MOV r2,#0x20        */
    m_w32(NULL, 0x08, 0xe12fff10);      /* BX  r0  -> Thumb    */
    m_w16(NULL, 0x10, 0x2105);          /* Thumb: MOV r1,#5    */
    m_w16(NULL, 0x12, 0x4710);          /* Thumb: BX r2 -> ARM */
    m_w32(NULL, 0x20, 0xe3a03009);      /* ARM:   MOV r3,#9    */

    arm_cpu_t c; arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
    for (int i = 0; i < 6; i++) arm_step(&c);

    CHECK(c.r[1] == 5, "r1=%u expect 5 (Thumb code ran)", c.r[1]);
    CHECK(c.r[3] == 9, "r3=%u expect 9 (returned to ARM)", c.r[3]);
    CHECK((c.cpsr & ARM_CPSR_T) == 0, "T bit should be clear back in ARM state");
}

static void test_bx_blx_register_reject_invalid_targets_without_side_effects(void) {
    arm_cpu_t c;

    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0u, 0xe12fff3fu);              /* BLX pc: UNPREDICTABLE */
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c.r[14] = 0xa5a5a5a5u;
    c.r[15] = 0u;
    uint32_t cpsr = c.cpsr;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[14] == 0xa5a5a5a5u &&
          c.r[15] == 0u && c.cpsr == cpsr,
          "ARM BLX pc changed LR/PC/state before trapping");

    memset(g_ram, 0, sizeof g_ram);
    m_w16(NULL, 0u, 0x47f8u);                  /* Thumb BLX pc */
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS | ARM_CPSR_T;
    c.r[14] = 0xa5a5a5a5u;
    c.r[15] = 0u;
    cpsr = c.cpsr;
    CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[14] == 0xa5a5a5a5u &&
          c.r[15] == 0u && c.cpsr == cpsr,
          "Thumb BLX pc changed LR/PC/state before trapping");

    static const struct {
        uint32_t arm;
        uint16_t thumb;
        const char *what;
    } cases[] = {
        { 0xe12fff10u, 0x4700u, "BX" },
        { 0xe12fff30u, 0x4780u, "BLX" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0u, cases[i].arm);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
        c.r[0] = 0x102u;                         /* no valid ARM/Thumb state */
        c.r[14] = 0xa5a5a5a5u;
        c.r[15] = 0u;
        cpsr = c.cpsr;
        CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[14] == 0xa5a5a5a5u &&
              c.r[15] == 0u && c.cpsr == cpsr,
              "ARM %s accepted target 0x102 or mutated state", cases[i].what);

        memset(g_ram, 0, sizeof g_ram);
        m_w16(NULL, 0u, cases[i].thumb);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS | ARM_CPSR_T;
        c.r[0] = 0x102u;
        c.r[14] = 0xa5a5a5a5u;
        c.r[15] = 0u;
        cpsr = c.cpsr;
        CHECK(arm_step(&c) == ARM_UNDEFINED && c.r[14] == 0xa5a5a5a5u &&
              c.r[15] == 0u && c.cpsr == cpsr,
              "Thumb %s accepted target 0x102 or mutated state", cases[i].what);
    }
}

static void test_thumb_bl_pair(void) {
    /* BL is a 32-bit pair in Thumb. The halves combine into one 22-bit offset:
     * target = (PC_of_first + 4) + (offset << 1). With offset 2 that is
     * 4 + 4 = 8. LR must be the address after the pair, with the Thumb bit set
     * so the eventual BX LR returns to Thumb state. */
    uint16_t p[] = { 0xf000, 0xf802 };
    arm_cpu_t c; load_thumb(&c, p, 2, 2);
    CHECK(c.r[15] == 0x08, "pc=%08x expect 08", c.r[15]);
    CHECK(c.r[14] == 0x05, "lr=%08x expect 05 (return addr | Thumb bit)", c.r[14]);
}

static void test_thumb_extend_and_reverse(void) {
    /* ARMv6 Thumb extend group. Real Apple LLB reaches UXTB within a few
     * thousand instructions, so these are required, not optional. */
    uint16_t p[] = { 0x21ff,   /* MOV  r1,#0xff        */
                     0xb2ca,   /* UXTB r2,r1  -> 0xff  */
                     0xb24b,   /* SXTB r3,r1  -> -1    */
                     0xb289 }; /* UXTH r1,r1  -> 0xff  */
    arm_cpu_t c; load_thumb(&c, p, 4, 4);
    CHECK(c.r[2] == 0xff, "r2=%08x expect ff (UXTB)", c.r[2]);
    CHECK(c.r[3] == 0xffffffffu, "r3=%08x expect ffffffff (SXTB)", c.r[3]);
    CHECK(c.r[1] == 0xff, "r1=%08x expect ff (UXTH)", c.r[1]);
}

static void test_thumb_rev(void) {
    /* Build 0x11223344 a byte at a time, then REV it. */
    uint16_t p[] = { 0x2011, 0x0200, 0x3022, 0x0200,
                     0x3033, 0x0200, 0x3044, 0xba01 };
    arm_cpu_t c; load_thumb(&c, p, 8, 8);
    CHECK(c.r[0] == 0x11223344u, "r0=%08x expect 11223344", c.r[0]);
    CHECK(c.r[1] == 0x44332211u, "r1=%08x expect 44332211 (REV)", c.r[1]);
}

static void test_thumb_cps(void) {
    /* CPSID i then CPSIE i must set and clear the CPSR I bit. */
    uint16_t p[] = { 0xb672, 0xb662 };
    arm_cpu_t c; load_thumb(&c, p, 2, 1);
    CHECK((c.cpsr & ARM_CPSR_I) != 0, "CPSID should mask IRQs");
    arm_step(&c);
    CHECK((c.cpsr & ARM_CPSR_I) == 0, "CPSIE should unmask IRQs");
}

static void test_mmu_supersection(void) {
    /* Regression from booting real XNU: a type-2 descriptor with bit 18 set is
     * a 16 MB SUPERsection, taking its base from bits[31:24] and the offset
     * from va[23:0] — a different split from the 1 MB section. Treating one as
     * a section silently resolves the wrong physical address, and the kernel
     * read garbage where a valid pointer lived. */
    arm_cpu_t c;
    memset(g_ram, 0, sizeof g_ram);
    /* Bits[8:5] are not a domain in a supersection. Give them a value whose
     * corresponding DACR domain is disabled: a walker that decodes them as a
     * normal section domain will spuriously fault instead of using domain 0. */
    uint32_t super = 0x08000000u | (1u << 18) | (3u << 10) |
                     (5u << 5) | 2u;
    for (unsigned i = 0; i < 16; i++)
        m_w32(NULL, 0x4000 + ((((0xc0000000u >> 20) + i)) << 2), super);
    arm_reset(&c, &g_bus);
    c.cp15.ttbr0 = 0x4000; c.cp15.dacr = 1u; c.cp15.sctlr |= ARM_SCTLR_M;

    uint32_t pa = 0;
    CHECK(arm_mmu_translate(&c, 0xc020e220u, ARM_ACCESS_READ, true, &pa) == 0,
          "supersection translation faulted");
    CHECK(pa == 0x0820e220u,
          "pa=%08x expect 0820e220 (supersection uses va[23:0])", pa);

    /* A plain section must still use the 1 MB split. */
    uint32_t sect = 0x08000000u | (3u << 10) | 2u;
    m_w32(NULL, 0x4000 + ((0xc0000000u >> 20) << 2), sect);
    CHECK(arm_mmu_translate(&c, 0xc0001234u, ARM_ACCESS_READ, true, &pa) == 0,
          "section faulted");
    CHECK(pa == 0x08001234u, "pa=%08x expect 08001234 (plain section)", pa);
}

static void test_thumb_blx_suffix_is_not_a_branch(void) {
    /* Regression from booting real XNU: 0xE000-0xE7FF is an unconditional
     * branch but 0xE800-0xEFFF is the SECOND half of a BLX pair, which returns
     * to ARM state. Treating the whole 0xExxx range as a branch sent BLX to a
     * garbage address, and the kernel executed a pointer table as code. */
    uint16_t p[] = { 0xf000, 0xe802 };
    arm_cpu_t c; load_thumb(&c, p, 2, 2);
    CHECK((c.cpsr & ARM_CPSR_T) == 0, "BLX suffix must switch back to ARM state");
    CHECK((c.r[15] & 3u) == 0, "pc=%08x must be word-aligned after BLX", c.r[15]);
    CHECK(c.r[14] == 0x05, "lr=%08x expect 05 (return addr | Thumb bit)", c.r[14]);

    /* A plain unconditional Thumb branch must still branch. */
    uint16_t b[] = { 0xe000, 0x2007 };
    arm_cpu_t c2; load_thumb(&c2, b, 2, 2);
    CHECK((c2.cpsr & ARM_CPSR_T) != 0, "a plain branch must stay in Thumb");
}

static void test_user_bank_stm(void) {
    /* STM with the S bit transfers the USER bank whatever mode we are in —
     * how a kernel saves user context on exception entry. Real XNU uses
     * "STMIA sp,{r0-r14}^". */
    arm_cpu_t c; arm_reset(&c, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    arm_set_mode(&c, ARM_MODE_USR);
    c.r[13] = 0xaaaa0000u; c.r[14] = 0xbbbb0000u;
    arm_set_mode(&c, ARM_MODE_SVC);
    c.r[13] = 0x800; c.r[14] = 0xcccc0000u;
    m_w32(NULL, 0, 0xe8cd6000u);          /* STMIA sp,{r13,r14}^  (S bit set) */
    c.r[15] = 0;
    arm_step(&c);
    CHECK(m_r32(NULL, 0x800) == 0xaaaa0000u,
          "stored r13=%08x expect the USER bank aaaa0000", m_r32(NULL, 0x800));
    CHECK(m_r32(NULL, 0x804) == 0xbbbb0000u,
          "stored r14=%08x expect the USER bank bbbb0000", m_r32(NULL, 0x804));
    CHECK(c.r[14] == 0xcccc0000u, "the SVC bank must be untouched");
}

/* ------------------------------------------------ TTBCR.N / TTBR1 split ---
 * ARM ARM (ARMv6) B4.9.4 "Selecting between TTBR0 and TTBR1":
 *
 *   N == 0  -> every VA walks TTBR0; TTBR1 is not used at all.
 *   N  > 0  -> if VA[31:32-N] are all zero the walk starts at TTBR0, whose
 *              table is 2^(14-N) bytes based at TTBR0[31:14-N] and indexed by
 *              VA[31-N:20] (12-N index bits); otherwise it starts at TTBR1,
 *              which is ALWAYS a full 16 KB table indexed by VA[31:20].
 *
 * The tests below plant a decoy descriptor wherever a plausible mis-implementation
 * (unmasked base, wrong index width, inverted or off-by-one selector, TTBR0
 * fall-back) would land, so a wrong walker resolves to a recognisable PA rather
 * than merely faulting.
 */

/* Point the CPU at a TTBR0/TTBR1 pair with a given TTBCR.N, MMU on, domain 0 a
 * client so the AP bits are really checked. Clears RAM — plant descriptors
 * after calling this, not before. */
static void mmu_setup_split(arm_cpu_t *c, unsigned n,
                            uint32_t ttbr0, uint32_t ttbr1) {
    memset(g_ram, 0, sizeof g_ram);
    arm_reset(c, &g_bus);
    c->cp15.ttbr0  = ttbr0;
    c->cp15.ttbr1  = ttbr1;
    c->cp15.ttbcr  = n;
    c->cp15.dacr   = 1u;                      /* domain 0: client */
    c->cp15.sctlr |= ARM_SCTLR_M;             /* MMU on */
}

/* Plant a 1 MB section (AP=11, domain 0) at an explicit first-level index. The
 * index is spelled out instead of derived from the VA because which table and
 * which index the walker picks is precisely what is under test. The base and
 * offset are OR-ed exactly as the walker combines them, so a decoy written at an
 * unmasked base lands where a mis-masked walker would read. */
static void mmu_put_section(uint32_t table, unsigned index, uint32_t pa) {
    m_w32(NULL, table | (index << 2), (pa & 0xfff00000u) | (3u << 10) | 2u);
}

/* Privileged read translation; returns the PA, or 0xffffffff if it faulted. */
static uint32_t mmu_xlate(arm_cpu_t *c, uint32_t va) {
    uint32_t pa = 0;
    return arm_mmu_translate(c, va, ARM_ACCESS_READ, true, &pa) ? 0xffffffffu : pa;
}

static void test_mmu_ttbcr_n0_ignores_ttbr1(void) {
    /* N == 0 is the reset state and must keep behaving exactly as the walker did
     * before the split existed: a single 4096-entry TTBR0 table covering the
     * whole 4 GB, based at TTBR0[31:14], with TTBR1 never consulted. */
    arm_cpu_t c; mmu_setup_split(&c, 0, 0x4200u, 0x8000u); /* base masks to 0x4000 */
    mmu_put_section(0x4000, 0x001, 0x00400000u);   /* VA 0x00100000 */
    mmu_put_section(0x4000, 0xc00, 0x00200000u);   /* VA 0xc0000000 */
    mmu_put_section(0x4000, 0xfff, 0x00500000u);   /* VA 0xfff00000, last entry */
    mmu_put_section(0x4200, 0x001, 0x00900000u);   /* decoy: unmasked TTBR0 base */
    mmu_put_section(0x8000, 0xc00, 0x00900000u);   /* decoy: TTBR1 must be ignored */
    mmu_put_section(0x8000, 0xfff, 0x00900000u);   /* decoy: TTBR1 must be ignored */

    uint32_t pa = mmu_xlate(&c, 0x00100abcu);
    CHECK(pa == 0x00400abcu, "pa=%08x expect 00400abc (low VA via TTBR0)", pa);
    pa = mmu_xlate(&c, 0xc0001234u);
    CHECK(pa == 0x00201234u, "pa=%08x expect 00201234 (N=0: high VA still TTBR0)", pa);
    pa = mmu_xlate(&c, 0xfff00010u);
    CHECK(pa == 0x00500010u, "pa=%08x expect 00500010 (N=0 table is 4096 entries)", pa);
}

static void test_mmu_ttbcr_n2_low_va_uses_shrunk_ttbr0(void) {
    /* N == 2 is what XNU-ARM runs with. TTBR0's table shrinks to 2^(14-2) = 4 KB
     * / 1024 entries, its base is TTBR0[31:12] (the low 12 bits of the register
     * are not part of the address), and it is indexed by VA[29:20] — ten bits. */
    arm_cpu_t c; mmu_setup_split(&c, 2, 0x5400u, 0x8000u); /* base masks to 0x5000 */
    mmu_put_section(0x5000, 0x001, 0x00700000u);   /* VA 0x00100000 */
    mmu_put_section(0x5000, 0x3ff, 0x00800000u);   /* VA 0x3ff00000, last entry */
    mmu_put_section(0x5400, 0x001, 0x00900000u);   /* decoy: unmasked TTBR0 base */

    uint32_t pa = mmu_xlate(&c, 0x00100abcu);
    CHECK(pa == 0x00700abcu,
          "pa=%08x expect 00700abc (N=2 TTBR0 base masks to a 4 KB boundary)", pa);
    pa = mmu_xlate(&c, 0x3ff00abcu);
    CHECK(pa == 0x00800abcu,
          "pa=%08x expect 00800abc (last entry of the 1024-entry TTBR0 table)", pa);
}

static void test_mmu_ttbcr_n2_kernel_va_uses_ttbr1(void) {
    /* With N == 2 everything at or above 0x40000000 walks TTBR1: all of kernel
     * text at 0xc0000000 and the 0xffff0000 exception-vector page. TTBR1's table
     * is a full 16 KB indexed by VA[31:20] regardless of N. */
    arm_cpu_t c; mmu_setup_split(&c, 2, 0x5000u, 0x8234u); /* base masks to 0x8000 */
    mmu_put_section(0x8000, 0xc00, 0x00200000u);   /* kernel text  0xc0000000 */
    mmu_put_section(0x8000, 0xfff, 0x00300000u);   /* vector page  0xffff0000 */
    mmu_put_section(0x8234, 0xc00, 0x00900000u);   /* decoy: unmasked TTBR1 base */
    /* 0xd0000000 has no TTBR1 entry. Give the TTBR0 table an entry at the index
     * a truncated walk would use (0xd00 & 0x3ff) so a fall-back is visible. */
    mmu_put_section(0x5000, 0x100, 0x00a00000u);

    uint32_t pa = mmu_xlate(&c, 0xc0000abcu);
    CHECK(pa == 0x00200abcu, "pa=%08x expect 00200abc (kernel text via TTBR1)", pa);
    /* 0xffff000c lies 0xf000c into the 1 MB section based at 0xfff00000. */
    pa = mmu_xlate(&c, 0xffff000cu);
    CHECK(pa == 0x003f000cu, "pa=%08x expect 003f000c (vector page via TTBR1)", pa);

    uint32_t p = 0;
    uint32_t f = arm_mmu_translate(&c, 0xd0000000u, ARM_ACCESS_READ, true, &p);
    CHECK((f & 0xf) == ARM_FSR_SECTION_TRANSLATION,
          "fsr=%x expect a translation fault: a TTBR1 miss must not fall back "
          "to TTBR0 (got pa=%08x)", f, p);
}

static void test_mmu_ttbcr_n2_split_boundary(void) {
    /* The selector with N == 2 is VA[31:30]: 0x3ff00000 is the last VA that
     * still walks TTBR0 and 0x40000000 is the first that crosses to TTBR1. */
    arm_cpu_t c; mmu_setup_split(&c, 2, 0x5000u, 0x8000u);
    mmu_put_section(0x5000, 0x3ff, 0x00100000u);   /* TTBR0 side of the line */
    mmu_put_section(0x8000, 0x400, 0x00200000u);   /* TTBR1 side of the line */
    /* 0x40000000's index truncated to ten bits is 0 — where the walk would land
     * if the index were shrunk but the table not switched. */
    mmu_put_section(0x5000, 0x000, 0x00900000u);
    /* and TTBR1 at the TTBR0-side index, in case the selector is inverted. */
    mmu_put_section(0x8000, 0x3ff, 0x00a00000u);

    uint32_t pa = mmu_xlate(&c, 0x3ff00abcu);
    CHECK(pa == 0x00100abcu,
          "pa=%08x expect 00100abc (0x3ff00000 is still TTBR0 with N=2)", pa);
    pa = mmu_xlate(&c, 0x40000abcu);
    CHECK(pa == 0x00200abcu,
          "pa=%08x expect 00200abc (0x40000000 is the first TTBR1 VA with N=2)", pa);
}

static void test_mmu_kernel_mapping_survives_ttbr0_pmap_switch(void) {
    /* THE regression the split exists to fix. XNU runs with N == 2, keeps the
     * kernel in TTBR1 and the *current user* pmap in TTBR0, and rewrites TTBR0
     * on every context switch (pmap_switch -> set_mmu_ttb). A walker that always
     * used TTBR0 lost kernel text and the 0xffff0000 vector page the instant
     * that happened, and the CPU stormed on prefetch aborts at 0xffff000c. */
    const uint32_t pmap_a = 0x5000u, pmap_b = 0x6000u, kernel = 0x8000u;
    arm_cpu_t c; mmu_setup_split(&c, 2, pmap_a, kernel);
    mmu_put_section(kernel, 0xc00, 0x00200000u);   /* kernel text  0xc0000000 */
    mmu_put_section(kernel, 0xfff, 0x00300000u);   /* vector page  0xffff0000 */
    mmu_put_section(pmap_a, 0x001, 0x00500000u);   /* user 0x00100000 under A */
    mmu_put_section(pmap_b, 0x001, 0x00600000u);   /* user 0x00100000 under B */

    uint32_t text_a = mmu_xlate(&c, 0xc0001000u);
    uint32_t vec_a  = mmu_xlate(&c, 0xffff000cu);
    uint32_t usr_a  = mmu_xlate(&c, 0x00100000u);
    CHECK(text_a == 0x00201000u, "pa=%08x expect 00201000 (kernel text, pmap A)", text_a);
    CHECK(vec_a  == 0x003f000cu, "pa=%08x expect 003f000c (vector page, pmap A)", vec_a);
    CHECK(usr_a  == 0x00500000u, "pa=%08x expect 00500000 (user VA via pmap A)", usr_a);

    c.cp15.ttbr0 = pmap_b;                          /* pmap_switch() to another task */

    uint32_t usr_b  = mmu_xlate(&c, 0x00100000u);
    CHECK(usr_b == 0x00600000u,
          "pa=%08x expect 00600000 — the TTBR0 switch must take effect for user VAs",
          usr_b);
    uint32_t text_b = mmu_xlate(&c, 0xc0001000u);
    uint32_t vec_b  = mmu_xlate(&c, 0xffff000cu);
    CHECK(text_b == text_a,
          "pa=%08x expect %08x — kernel text must survive a TTBR0 pmap switch",
          text_b, text_a);
    CHECK(vec_b == vec_a,
          "pa=%08x expect %08x — the vector page must survive a TTBR0 pmap switch",
          vec_b, vec_a);
}

static void test_mmu_ttbcr_n1_table_geometry(void) {
    /* N == 1: TTBR0's table is 2^(14-1) = 8 KB / 2048 entries based at
     * TTBR0[31:13] and indexed by VA[30:20]; the split falls at 0x80000000. */
    arm_cpu_t c; mmu_setup_split(&c, 1, 0x6800u, 0x8000u); /* base masks to 0x6000 */
    mmu_put_section(0x6000, 0x001, 0x00300000u);   /* VA 0x00100000 */
    mmu_put_section(0x6000, 0x7ff, 0x00100000u);   /* VA 0x7ff00000, last entry */
    mmu_put_section(0x8000, 0x800, 0x00200000u);   /* VA 0x80000000, first TTBR1 VA */
    mmu_put_section(0x6800, 0x001, 0x00900000u);   /* decoy: unmasked TTBR0 base */
    mmu_put_section(0x6000, 0x000, 0x00a00000u);   /* decoy: index shrunk, table not switched */

    uint32_t pa = mmu_xlate(&c, 0x00100abcu);
    CHECK(pa == 0x00300abcu,
          "pa=%08x expect 00300abc (N=1 TTBR0 base masks to an 8 KB boundary)", pa);
    pa = mmu_xlate(&c, 0x7ff00abcu);
    CHECK(pa == 0x00100abcu,
          "pa=%08x expect 00100abc (0x7ff00000 is the last TTBR0 VA with N=1)", pa);
    pa = mmu_xlate(&c, 0x80000abcu);
    CHECK(pa == 0x00200abcu,
          "pa=%08x expect 00200abc (0x80000000 is the first TTBR1 VA with N=1)", pa);
}

static void test_mmu_ttbcr_n3_table_geometry(void) {
    /* N == 3: TTBR0's table is 2^(14-3) = 2 KB / 512 entries based at
     * TTBR0[31:11] and indexed by VA[28:20]; the split falls at 0x20000000.
     * TTBR1's table stays a full 16 KB indexed by VA[31:20] whatever N is. */
    arm_cpu_t c; mmu_setup_split(&c, 3, 0x6400u, 0x8000u); /* base masks to 0x6000 */
    mmu_put_section(0x6000, 0x001, 0x00300000u);   /* VA 0x00100000 */
    mmu_put_section(0x6000, 0x1ff, 0x00100000u);   /* VA 0x1ff00000, last entry */
    mmu_put_section(0x8000, 0x200, 0x00200000u);   /* VA 0x20000000, first TTBR1 VA */
    mmu_put_section(0x8000, 0xfff, 0x00400000u);   /* VA 0xfff00000 via TTBR1 */
    mmu_put_section(0x6400, 0x001, 0x00900000u);   /* decoy: unmasked TTBR0 base */
    mmu_put_section(0x6000, 0x000, 0x00a00000u);   /* decoy: index shrunk, table not switched */

    uint32_t pa = mmu_xlate(&c, 0x00100abcu);
    CHECK(pa == 0x00300abcu,
          "pa=%08x expect 00300abc (N=3 TTBR0 base masks to a 2 KB boundary)", pa);
    pa = mmu_xlate(&c, 0x1ff00abcu);
    CHECK(pa == 0x00100abcu,
          "pa=%08x expect 00100abc (0x1ff00000 is the last TTBR0 VA with N=3)", pa);
    pa = mmu_xlate(&c, 0x20000abcu);
    CHECK(pa == 0x00200abcu,
          "pa=%08x expect 00200abc (0x20000000 is the first TTBR1 VA with N=3)", pa);
    pa = mmu_xlate(&c, 0xffff000cu);
    CHECK(pa == 0x004f000cu,
          "pa=%08x expect 004f000c (TTBR1 is a full 16 KB table for every N)", pa);
}

static void test_mmu_ttbcr_pd_bits_suppress_selected_walk(void) {
    uint32_t pa, fsr;
    arm_cpu_t c;

    /* PD0 disables a low-VA TTBR0 walk even when a valid descriptor exists. */
    mmu_setup_split(&c, 2u, 0x5000u, 0x8000u);
    mmu_put_section(0x5000u, 1u, 0x00300000u);
    c.cp15.ttbcr |= 1u << 4;
    g_watch_addr = 0x5004u;
    g_watch_reads32 = 0u;
    pa = 0xdeadbeefu;
    fsr = arm_mmu_translate(&c, 0x00100000u, ARM_ACCESS_READ, true, &pa);
    CHECK((fsr & 0xfu) == ARM_FSR_SECTION_TRANSLATION &&
          g_watch_reads32 == 0u && pa == 0xdeadbeefu,
          "PD0 fsr=%x reads=%u pa=%08x", fsr, g_watch_reads32, pa);

    /* PD0 applies only to TTBR0. The same setting must not block TTBR1. */
    mmu_put_section(0x8000u, 0xc00u, 0x00400000u);
    pa = mmu_xlate(&c, 0xc0000123u);
    CHECK(pa == 0x00400123u, "PD0 blocked TTBR1: pa=%08x", pa);

    /* Conversely PD1 suppresses the high-VA TTBR1 walk without touching its
     * first-level descriptor, but leaves the low-VA TTBR0 walk operational. */
    mmu_setup_split(&c, 2u, 0x5000u, 0x8000u);
    mmu_put_section(0x5000u, 1u, 0x00300000u);
    mmu_put_section(0x8000u, 0xc00u, 0x00400000u);
    c.cp15.ttbcr |= 1u << 5;
    g_watch_addr = 0xb000u;
    g_watch_reads32 = 0u;
    pa = 0xdeadbeefu;
    fsr = arm_mmu_translate(&c, 0xc0000123u, ARM_ACCESS_WRITE, true, &pa);
    CHECK((fsr & 0xfu) == ARM_FSR_SECTION_TRANSLATION &&
          (fsr & (1u << 11)) != 0u && g_watch_reads32 == 0u &&
          pa == 0xdeadbeefu,
          "PD1 fsr=%x reads=%u pa=%08x", fsr, g_watch_reads32, pa);
    pa = mmu_xlate(&c, 0x00100000u);
    CHECK(pa == 0x00300000u, "PD1 blocked TTBR0: pa=%08x", pa);

    g_watch_addr = 0xffffffffu;
}

/* ------------------------------------------------- DFSR / IFSR completeness -
 * The fault status registers are the whole conversation between this CPU and
 * XNU's abort handlers, and a single missing bit in them cost a full diagnosis
 * cycle (DFSR.WnR, commit 85c4653). These tests pin down every field the
 * kernel is known to read.
 *
 * What xnu-1357.5.30 actually reads, from the shipped kernel:
 *   _fleh_dataabt (0xc0068338)  SUB lr,lr,#8 ; MRC p15,0,r5,c5,c0,0 (DFSR)
 *                                            ; MRC p15,0,r6,c6,c0,0 (DFAR)
 *   _fleh_prefabt (0xc006828c)  SUB lr,lr,#4 ; MRC p15,0,r1,c6,c0,2 (IFAR)
 *                                            ; MRC p15,0,r5,c5,c0,1 (IFSR)
 *   _sleh_abort   (0xc006c538)  status = fsr & 0x40f, and for a data abort
 *                               TST fsr,#0x800 — DFSR.WnR — selecting
 *                               fault_type 3 (read|write) instead of 1 (read).
 */

/* Two 1 MB sections through one L1 table at 0x4000: an identity map for the
 * low megabyte (so the instruction fetch itself always succeeds) and a test
 * mapping at 0x80000000 with the caller's AP. 0x80100000 is deliberately left
 * without a descriptor. `mode` is the mode to execute in. */
static void fsr_setup(arm_cpu_t *c, const uint32_t *prog, size_t words,
                      unsigned ap, uint32_t mode) {
    memset(g_ram, 0, sizeof g_ram);
    for (size_t i = 0; i < words; i++) m_w32(NULL, (uint32_t)(i * 4), prog[i]);
    m_w32(NULL, 0x4000 + (0x000u << 2), 0x00000000u | (3u << 10) | 2u);
    m_w32(NULL, 0x4000 + (0x800u << 2), 0x00000000u | (ap  << 10) | 2u);
    arm_reset(c, &g_bus);
    c->cp15.ttbr0 = 0x4000; c->cp15.dacr = 1u; c->cp15.sctlr |= ARM_SCTLR_M;
    arm_set_mode(c, mode);
    /* Reset leaves CPSR.A set. Clear it so the assertions below prove that
     * *exception entry* sets it, rather than passing on a leftover. */
    c->cpsr &= ~ARM_CPSR_A;
    c->r[15] = 0;
}

static void test_dfsr_wnr_write_vs_read(void) {
    /* At the translator itself: the SAME fault, reached by a read and by a
     * write, must differ in exactly one bit. AP=00 is "no access", so both
     * directions fault with the same status and the only difference is WnR. */
    arm_cpu_t c; mmu_setup_section(&c, 0x80000000u, 0x00200000u, 0, 0);
    uint32_t pa = 0;
    uint32_t rd = arm_mmu_translate(&c, 0x80000abcu, ARM_ACCESS_READ, true, &pa);
    uint32_t wr = arm_mmu_translate(&c, 0x80000abcu, ARM_ACCESS_WRITE, true, &pa);
    CHECK((rd & 0xfu) == ARM_FSR_SECTION_PERMISSION,
          "fsr=%08x expect a section permission fault on read", rd);
    CHECK((wr & 0xfu) == ARM_FSR_SECTION_PERMISSION,
          "fsr=%08x expect a section permission fault on write", wr);
    CHECK((rd & (1u << 11)) == 0, "fsr=%08x: WnR must be CLEAR for a read", rd);
    CHECK((wr & (1u << 11)) != 0, "fsr=%08x: WnR must be SET for a write", wr);
    CHECK((rd ^ wr) == (1u << 11),
          "read fsr %08x and write fsr %08x must differ in bit 11 alone", rd, wr);

    /* WnR is orthogonal to the status code: a translation fault carries it too,
     * and so does an unprivileged access. */
    uint32_t tw = arm_mmu_translate(&c, 0x90000000u, ARM_ACCESS_WRITE, false, &pa);
    CHECK((tw & 0xfu) == ARM_FSR_SECTION_TRANSLATION,
          "fsr=%08x expect a translation fault", tw);
    CHECK((tw & (1u << 11)) != 0,
          "fsr=%08x: WnR must be SET on a write translation fault too", tw);

    /* And no field above bit 11 is ever invented: ExT (bit 12) and the
     * extended status bit (bit 10) have no source in this machine. */
    CHECK((wr & ~0x8ffu) == 0,
          "fsr=%08x has bits set outside status/domain/WnR", wr);
}

static void test_data_abort_write_sets_dfsr_wnr(void) {
    /* End to end, the way XNU sees it: an unprivileged store to a
     * privileged-only page. This is the exact shape of the copyout that
     * livelocked ~2.8 million times with WnR missing. */
    uint32_t p[] = { 0xe3a01102 /*MOV r1,#0x80000000*/,
                     0xe5812000 /*STR r2,[r1]        */ };
    arm_cpu_t c; fsr_setup(&c, p, 2, 1 /*AP=01: privileged RW only*/, ARM_MODE_USR);
    arm_step(&c);
    uint32_t before = c.cpsr;
    arm_step(&c);

    CHECK(c.r[15] == ARM_VEC_DATA_ABORT, "pc=%08x expect 10 (data abort)", c.r[15]);
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_SECTION_PERMISSION,
          "dfsr=%08x expect a section permission fault", c.cp15.dfsr);
    CHECK((c.cp15.dfsr & (1u << 11)) != 0,
          "dfsr=%08x: bit 11 (WnR) must be SET for a store", c.cp15.dfsr);
    CHECK(c.cp15.dfar == 0x80000000u, "dfar=%08x expect 80000000", c.cp15.dfar);

    /* Abort-mode entry state, checked against what fleh_dataabt assumes:
     * it does SUB lr,lr,#8 to recover the faulting instruction's address, and
     * then LDRs the instruction word from it to classify the access. */
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_ABT,
          "mode=%02x expect ABT", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(c.r[14] == 4u + 8u, "lr=%08x expect 0c (faulting insn 0x04 + 8)", c.r[14]);
    CHECK(c.spsr[ARM_BANK_ABT] == before,
          "spsr=%08x expect the pre-fault cpsr %08x", c.spsr[ARM_BANK_ABT], before);
    CHECK((c.cpsr & ARM_CPSR_I) != 0, "IRQs must be masked on abort entry");
    CHECK((c.cpsr & ARM_CPSR_A) != 0,
          "cpsr=%08x: ARMv6 sets CPSR.A on data-abort entry", c.cpsr);
    CHECK((c.cpsr & ARM_CPSR_T) == 0, "exceptions must enter in ARM state");

    /* The same page, same instruction shape, read instead of write. */
    uint32_t q[] = { 0xe3a01102 /*MOV r1,#0x80000000*/,
                     0xe5912000 /*LDR r2,[r1]        */ };
    arm_cpu_t d; fsr_setup(&d, q, 2, 1, ARM_MODE_USR);
    arm_step(&d); arm_step(&d);
    CHECK(d.r[15] == ARM_VEC_DATA_ABORT, "pc=%08x expect 10 (data abort)", d.r[15]);
    CHECK((d.cp15.dfsr & 0xfu) == ARM_FSR_SECTION_PERMISSION,
          "dfsr=%08x expect a section permission fault", d.cp15.dfsr);
    CHECK((d.cp15.dfsr & (1u << 11)) == 0,
          "dfsr=%08x: bit 11 (WnR) must be CLEAR for a load", d.cp15.dfsr);
}

static void test_prefetch_abort_never_sets_wnr(void) {
    /* IFSR has no WnR field. The fetch path must translate as a read, and it
     * must leave DFSR/DFAR — which belong to the data side — untouched, because
     * a prefetch abort taken while a data fault is still recorded would
     * otherwise rewrite the kernel's view of the earlier fault. */
    uint32_t p[] = { 0xe3a00102 /*MOV r0,#0x80000000*/,
                     0xe1a0f000 /*MOV pc,r0         */ };
    arm_cpu_t c; fsr_setup(&c, p, 2, 3, ARM_MODE_SYS);
    c.cp15.dfsr = 0xdeadbeefu; c.cp15.dfar = 0xcafebabeu;   /* sentinels */
    arm_step(&c);
    /* 0x80100000 is not an encodable ARM immediate, so aim the branch there by
     * hand: it is the megabyte just past the mapping, with no descriptor. */
    c.r[0] = 0x80100000u;
    arm_step(&c);                            /* MOV pc,r0 */
    CHECK(c.r[15] == 0x80100000u, "pc=%08x expect 80100000 before the fetch", c.r[15]);
    uint32_t before = c.cpsr;
    arm_step(&c);                            /* the fetch aborts */

    CHECK(c.r[15] == ARM_VEC_PREFETCH, "pc=%08x expect 0c (prefetch abort)", c.r[15]);
    CHECK((c.cp15.ifsr & 0xfu) == ARM_FSR_SECTION_TRANSLATION,
          "ifsr=%08x expect a section translation fault", c.cp15.ifsr);
    CHECK((c.cp15.ifsr & (1u << 11)) == 0,
          "ifsr=%08x: the instruction-fetch path must never set bit 11 — "
          "IFSR has no WnR field", c.cp15.ifsr);
    CHECK(c.cp15.ifar == 0x80100000u, "ifar=%08x expect 80100000", c.cp15.ifar);
    CHECK(c.cp15.dfsr == 0xdeadbeefu,
          "dfsr=%08x: a prefetch abort must not touch the data-side FSR", c.cp15.dfsr);
    CHECK(c.cp15.dfar == 0xcafebabeu,
          "dfar=%08x: a prefetch abort must not touch the data-side FAR", c.cp15.dfar);
    /* fleh_prefabt does SUB lr,lr,#4 to recover the faulting address. */
    CHECK(c.r[14] == 0x80100000u + 4u, "lr=%08x expect 80100004", c.r[14]);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_ABT,
          "mode=%02x expect ABT", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(c.spsr[ARM_BANK_ABT] == before,
          "spsr=%08x expect the pre-fault cpsr %08x", c.spsr[ARM_BANK_ABT], before);
    CHECK((c.cpsr & ARM_CPSR_A) != 0,
          "cpsr=%08x: ARMv6 sets CPSR.A on prefetch-abort entry", c.cpsr);
}

static void test_dfar_is_the_faulting_word_of_a_block_transfer(void) {
    /* An LDM that runs off the end of a mapped page must report the address of
     * the word that actually faulted, not the base of the transfer. sleh_abort
     * page-aligns DFAR (fp & 0xfffff000) and hands it to arm_fast_fault, so
     * reporting the base would map in the page the transfer already had and
     * re-execute the same instruction forever. */
    uint32_t p[] = { 0xe59f1008 /*LDR  r1,[pc,#8] -> the literal at 0x10 */,
                     0xe8910007 /*LDMIA r1,{r0,r1,r2}                    */,
                     0xeafffffe /*B .                                    */,
                     0x00000000 /*(pad)                                  */,
                     0x800ffff8 /*literal: 8 bytes before the page end   */ };
    arm_cpu_t c; fsr_setup(&c, p, 5, 3, ARM_MODE_SYS);
    c.r[0] = 0x11111111u; c.r[2] = 0x33333333u;
    arm_step(&c);
    CHECK(c.r[1] == 0x800ffff8u, "r1=%08x expect 800ffff8", c.r[1]);
    arm_step(&c);

    CHECK(c.r[15] == ARM_VEC_DATA_ABORT, "pc=%08x expect 10 (data abort)", c.r[15]);
    CHECK(c.cp15.dfar == 0x80100000u,
          "dfar=%08x expect 80100000 — the third word of the LDM, not the base "
          "0x800ffff8", c.cp15.dfar);
    CHECK((c.cp15.dfsr & (1u << 11)) == 0,
          "dfsr=%08x: an LDM is a read, WnR must be clear", c.cp15.dfsr);
    /* Base Restored Abort Model: nothing in the register file moved. */
    CHECK(c.r[0] == 0x11111111u, "r0=%08x expect 11111111 (base-restored)", c.r[0]);
    CHECK(c.r[1] == 0x800ffff8u, "r1=%08x expect 800ffff8 (base-restored)", c.r[1]);
    CHECK(c.r[2] == 0x33333333u, "r2=%08x expect 33333333 (base-restored)", c.r[2]);

    /* The mirror-image STM must set WnR and report the same address. */
    uint32_t q[] = { 0xe59f1008, 0xe8810007 /*STMIA r1,{r0,r1,r2}*/,
                     0xeafffffe, 0x00000000, 0x800ffff8 };
    arm_cpu_t d; fsr_setup(&d, q, 5, 3, ARM_MODE_SYS);
    arm_step(&d); arm_step(&d);
    CHECK(d.cp15.dfar == 0x80100000u,
          "dfar=%08x expect 80100000 for the storing form too", d.cp15.dfar);
    CHECK((d.cp15.dfsr & (1u << 11)) != 0,
          "dfsr=%08x: an STM is a write, WnR must be set", d.cp15.dfsr);
}

static void test_cps_is_a_nop_in_user_mode(void) {
    /* "CPS is a no-op if executed in User mode" (ARM ARM A4.1.16). Executing it
     * anyway lets a user program run "CPS #0x13" and continue in SVC with its
     * own registers. Nothing in a kernel-only boot can expose this, because XNU
     * only ever reaches CPS from a privileged mode. */
    uint32_t p[] = { 0xf1020013 /*CPS #0x13 (to SVC)*/,
                     0xf1080080 /*CPSIE i           */,
                     0xe3a00007 /*MOV r0,#7         */ };
    arm_cpu_t c; arm_reset(&c, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    for (unsigned i = 0; i < 3; i++) m_w32(NULL, i * 4, p[i]);
    arm_set_mode(&c, ARM_MODE_USR);
    c.cpsr |= ARM_CPSR_I;
    c.r[15] = 0;
    for (int i = 0; i < 3; i++) arm_step(&c);

    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR,
          "mode=%02x expect USR — CPS must not change mode from User mode",
          c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK((c.cpsr & ARM_CPSR_I) != 0,
          "cpsr=%08x: CPSIE from User mode must not unmask interrupts", c.cpsr);
    CHECK(c.r[0] == 7, "r0=%u expect 7 — CPS is a no-op, not a trap", c.r[0]);

    /* From a privileged mode it must still work, or the kernel cannot mask
     * interrupts at all. */
    arm_cpu_t d; arm_reset(&d, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xf1020013u);
    arm_set_mode(&d, ARM_MODE_SYS);
    d.r[15] = 0;
    arm_step(&d);
    CHECK((d.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC,
          "mode=%02x expect SVC — privileged CPS must still change mode",
          d.cpsr & ARM_CPSR_MODE_MASK);
}

/* ------------------------------------------------- lazy VFP / undefined ---
 *
 * XNU clears FPEXC.EN and enables VFP one thread at a time: the first VFP
 * instruction a thread runs is SUPPOSED to be undefined, and _sleh_undef turns
 * VFP on and re-runs it. So a VFP encoding with VFP off must vector the guest
 * — but the same encoding with VFP already ON must still stop the machine,
 * because at that point the kernel's own handler would call it an illegal
 * instruction and we would have converted "not implemented" into a SIGILL.
 */

/* Run one instruction with the CPU privileged, VFP granted by CPACR, and
 * FPEXC.EN as asked. Returns the step status. */
static arm_status_t run_vfp(arm_cpu_t *c, uint32_t insn, bool en) {
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, insn);
    arm_reset(c, &g_bus);
    c->cpsr = (c->cpsr & ~0x1fu) | ARM_MODE_SYS;
    c->cp15.cpacr  = 0xfu << ARM_CPACR_CP10_SHIFT;   /* full access to both */
    c->vfp_fpexc   = en ? ARM_FPEXC_EN : 0u;
    c->r[15] = 0;
    return arm_step(c);
}

static void test_vfp_disabled_vectors_the_guest(void) {
    /* Every encoding _sleh_undef (0xc006c368) matches, one per mask. */
    struct { uint32_t insn; const char *what; } v[] = {
        { 0xf2000d40u, "VADD.F32 (Advanced SIMD data processing)" },
        { 0xee300a00u, "VADD.F32 s0,s0,s0 (VFP data processing)"  },
        { 0xed901a00u, "VLDR s2,[r0] (VFP load/store)"            },
        { 0xecb10a20u, "VLDMIA r1!,{s0-s31} (VFP load/store)"     },
        { 0xf4200a0fu, "VLD1.8 (SIMD element/structure ld/st)"    },
        { 0xee100a10u, "VMOV r0,s0 (VFP 32-bit transfer)"         },
        { 0xee274b10u, "FMDHR d7,r4 (VFPv2 D-word transfer)"       },
        { 0xec510b10u, "VMOV r0,r1,d0 (VFP 64-bit transfer)"      },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        arm_cpu_t c;
        arm_status_t st = run_vfp(&c, v[i].insn, false);
        CHECK(st == ARM_OK && c.r[15] == ARM_VEC_UNDEFINED,
              "%s with FPEXC.EN=0: status=%d pc=%08x, expect a guest vector to 0x04",
              v[i].what, (int)st, c.r[15]);
    }
}

static void test_vfp_enabled_still_halts(void) {
    /* The other half of the rule, and the half that keeps us honest: once the
     * kernel has enabled VFP, an encoding we cannot execute has to stop the
     * machine and name itself. _sleh_undef would deliver EXC_BAD_INSTRUCTION
     * here, so vectoring would destroy the diagnostic AND kill the process.
     *
     * VFPv2 itself is now implemented (core/src/arm/vfp.c), so the examples
     * here are the encodings that remain genuinely absent from this part: NEON
     * — which the ARM1176 does not have at all — and VFPv3 additions. The VFP
     * unit's own coverage is tested in core/tests/test_vfp.c. */
    struct { uint32_t insn; const char *what; } v[] = {
        { 0xf2000d40u, "VADD.F32 (Advanced SIMD; no NEON on the ARM1176)" },
        { 0xf4200a0fu, "VLD1.8 (Advanced SIMD element/structure load)"    },
        { 0xeeb00b00u, "VMOV.F64 d0,#imm (VFPv3)"                        },
        { 0xee400b10u, "VMOV.8 d0[0],r0 (Advanced SIMD scalar transfer)"   },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        arm_cpu_t c;
        arm_status_t st = run_vfp(&c, v[i].insn, true);
        CHECK(st == ARM_UNDEFINED,
              "%s with FPEXC.EN=1: status=%d, expect ARM_UNDEFINED — we would "
              "have to compute a real floating-point result", v[i].what, (int)st);
    }
}

static void test_non_vfp_undefined_still_halts(void) {
    /* Encodings outside the six masks must be untouched by all of this, or the
     * lazy-VFP path becomes a silent swallow-everything. 0xE7FFDEFE is the
     * ARM breakpoint: _sleh_undef recognises it too, but it means the guest
     * trapped deliberately and we want to see where. */
    struct { uint32_t insn; const char *what; } v[] = {
        { 0xe7ffdefeu, "BKPT (0xE7FFDEFE)"           },
        { 0xf1010200u, "SETEND BE"                   },
        { 0xee000d10u, "MCR p13 (a coprocessor we do not model)" },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        arm_cpu_t c;
        arm_status_t st = run_vfp(&c, v[i].insn, false);
        CHECK(st == ARM_UNDEFINED,
              "%s: status=%d, expect ARM_UNDEFINED even with VFP disabled",
              v[i].what, (int)st);
    }
}

static void test_vfp_undef_entry_state(void) {
    /* The exception the guest actually receives. _fleh_undef does
     * "MRS sp,spsr; TST sp,#0x20; SUBEQ lr,lr,#4" — so LR must be the faulting
     * PC + 4 in ARM state, or the kernel resumes at the wrong instruction. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x100u, 0xee300a00u);            /* VADD.F32 s0,s0,s0 */
    arm_cpu_t c; arm_reset(&c, &g_bus);
    arm_set_mode(&c, ARM_MODE_USR);
    c.cpsr &= ~(ARM_CPSR_I | ARM_CPSR_A);
    c.cp15.cpacr = 0xfu << ARM_CPACR_CP10_SHIFT;
    c.vfp_fpexc  = 0;
    c.r[15] = 0x100u;
    uint32_t before = c.cpsr;
    arm_status_t st = arm_step(&c);

    CHECK(st == ARM_OK, "status=%d expect ARM_OK (the guest handles this)", (int)st);
    CHECK(c.r[15] == ARM_VEC_UNDEFINED, "pc=%08x expect the 0x04 vector", c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_UND,
          "mode=%02x expect Undefined (0x1b)", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(c.r[14] == 0x104u,
          "lr=%08x expect 0x104 — _fleh_undef subtracts 4 to find the faulting PC",
          c.r[14]);
    CHECK(c.spsr[ARM_BANK_UND] == before,
          "spsr=%08x expect the CPSR at fault time %08x", c.spsr[ARM_BANK_UND], before);
    CHECK((c.cpsr & ARM_CPSR_I) != 0, "cpsr=%08x: entry must mask IRQ", c.cpsr);
    CHECK((c.cpsr & ARM_CPSR_A) == 0,
          "cpsr=%08x: Undefined must NOT set CPSR.A (only aborts and interrupts do)",
          c.cpsr);
    CHECK((c.cpsr & ARM_CPSR_T) == 0, "cpsr=%08x: entry is always ARM state", c.cpsr);

    /* SCTLR.V moves the whole table, this vector included. */
    arm_cpu_t d; arm_reset(&d, &g_bus);
    d.cpsr = (d.cpsr & ~0x1fu) | ARM_MODE_SYS;
    d.cp15.sctlr |= ARM_SCTLR_V;
    d.cp15.cpacr  = 0xfu << ARM_CPACR_CP10_SHIFT;
    d.vfp_fpexc   = 0;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xee300a00u);
    d.r[15] = 0;
    arm_step(&d);
    CHECK(d.r[15] == 0xffff0004u,
          "pc=%08x expect 0xffff0004 with high vectors", d.r[15]);
}

static void test_vfp_sysreg_access_follows_fpexc(void) {
    /* FPEXC and FPSID stay readable with VFP off — that is what makes lazy
     * enabling possible, and _get_vfp_enabled (0xc006994c) does exactly this
     * VMRS with EN clear. FPSCR does not: accessing it with EN==0 is
     * UNDEFINED, and _vfp_switch is careful to set FPEXC.EN first. */
    arm_cpu_t c;
    arm_status_t st = run_vfp(&c, 0xeef80a10u, false);   /* VMRS r0, FPEXC */
    CHECK(st == ARM_OK && c.r[0] == 0,
          "VMRS FPEXC with EN=0: status=%d r0=%08x, must read back, not trap",
          (int)st, c.r[0]);

    st = run_vfp(&c, 0xeef00a10u, false);                /* VMRS r0, FPSID */
    CHECK(st == ARM_OK && c.r[0] == ARM1176_FPSID,
          "VMRS FPSID with EN=0: status=%d r0=%08x expect %08x",
          (int)st, c.r[0], ARM1176_FPSID);

    st = run_vfp(&c, 0xeef10a10u, false);                /* VMRS r0, FPSCR */
    CHECK(st == ARM_OK && c.r[15] == ARM_VEC_UNDEFINED,
          "VMRS FPSCR with EN=0: status=%d pc=%08x, must take the guest's "
          "Undefined exception", (int)st, c.r[15]);

    st = run_vfp(&c, 0xeef10a10u, true);                 /* VMRS r0, FPSCR */
    CHECK(st == ARM_OK && c.r[15] == 4u,
          "VMRS FPSCR with EN=1: status=%d pc=%08x, must simply execute",
          (int)st, c.r[15]);

    /* Setting FPEXC.EN is the whole point of the handler; it must stick. */
    st = run_vfp(&c, 0xeee80a10u, false);                /* VMSR FPEXC, r0 */
    CHECK(st == ARM_OK, "VMSR FPEXC with EN=0: status=%d, must be permitted", (int)st);
}

static void test_vfp_cpacr_denial_vectors(void) {
    /* CPACR gates CP10/CP11 ahead of FPEXC. XNU's _init_vfp writes 0xf<<20 to
     * grant both; before that runs, or if a field is set to "privileged only",
     * the access is UNDEFINED and the guest must see it. */
    arm_cpu_t c;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xee300a00u);                  /* VADD.F32 */
    arm_reset(&c, &g_bus);
    c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
    c.cp15.cpacr = 0;                             /* access denied */
    c.vfp_fpexc  = ARM_FPEXC_EN;                  /* enabled, but unreachable */
    c.r[15] = 0;
    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[15] == ARM_VEC_UNDEFINED,
          "CPACR=0 with FPEXC.EN=1: status=%d pc=%08x, CPACR must win",
          (int)st, c.r[15]);

    /* "Privileged only" (0b01 in both fields) denies User mode and permits SYS. */
    arm_cpu_t d; arm_reset(&d, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xeef10a10u);                  /* VMRS r0, FPSCR */
    arm_set_mode(&d, ARM_MODE_USR);
    d.cp15.cpacr = (1u << ARM_CPACR_CP10_SHIFT) | (1u << ARM_CPACR_CP11_SHIFT);
    d.vfp_fpexc  = ARM_FPEXC_EN;
    d.r[15] = 0;
    st = arm_step(&d);
    CHECK(st == ARM_OK && d.r[15] == ARM_VEC_UNDEFINED,
          "CPACR=privileged-only from User mode: status=%d pc=%08x, expect the "
          "guest vector", (int)st, d.r[15]);

    arm_cpu_t e; arm_reset(&e, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xeef10a10u);
    e.cpsr = (e.cpsr & ~0x1fu) | ARM_MODE_SYS;
    e.cp15.cpacr = (1u << ARM_CPACR_CP10_SHIFT) | (1u << ARM_CPACR_CP11_SHIFT);
    e.vfp_fpexc  = ARM_FPEXC_EN;
    e.r[15] = 0;
    st = arm_step(&e);
    CHECK(st == ARM_OK && e.r[15] == 4u,
          "CPACR=privileged-only from SYS: status=%d pc=%08x, expect it to run",
          (int)st, e.r[15]);
}

static void test_vfp_lazy_trap_cannot_loop(void) {
    /* The property that makes vectoring safe: the trap is one-shot. Enable VFP
     * the way _vfp_switch does and the SAME instruction now halts loudly
     * instead of vectoring again, so a kernel that enables VFP and retries
     * gets a diagnostic rather than an infinite exception loop. */
    /* 0xeeb00b00 is VMOV.F64 d0,#imm — in the VFP encoding space (so the
     * disabled case vectors) but a VFPv3 addition the VFP11 does not have (so
     * the enabled case halts). It has to be an encoding we do not implement:
     * one we DO implement simply executes on the retry, which is the whole
     * point of the milestone and is covered by test_vfp.c. */
    arm_cpu_t c;
    arm_status_t st = run_vfp(&c, 0xeeb00b00u, false);
    CHECK(st == ARM_OK && c.r[15] == ARM_VEC_UNDEFINED, "first pass must vector");

    c.vfp_fpexc |= ARM_FPEXC_EN;                  /* what _vfp_switch does */
    arm_set_mode(&c, ARM_MODE_SYS);
    c.r[15] = 0;
    st = arm_step(&c);
    CHECK(st == ARM_UNDEFINED,
          "status=%d after the guest enabled VFP: the retry must halt, not "
          "vector a second time", (int)st);
}

/* ================= the user <-> kernel transition =========================
 *
 * Everything below is checked against xnu-1357.5.30's own code, not only
 * against the ARM ARM, because the kernel's assembly is the other half of the
 * contract. The encodings used here are the literal words in firmware/
 * kernel.macho:
 *
 *   _fleh_swi              0xc00680a0  STM sp,{r0-r14}^   0xe8cd7fff
 *                          0xc00680b8  SRSIA sp,#0x13     0xf8cd0513
 *   _thread_exception_return
 *                          0xc0068774  LDM sp,{r0-r14}^   0xe8dd7fff
 *                          0xc006877c  MOVS pc,lr         0xe1b0f00e
 *   _fleh_fiq_generic      0xc00685e4  SUBSPL pc,lr,#4    0x525ef004
 *   _copyin                0xc0069cb0  LDRT r3,[r0],#4    0xe4b03004
 *   _copyout               0xc0069d38  STRT r3,[r1],#4    0xe4a13004
 *   _thread_set_cthread_self
 *                          0xc0061da0  MCR p15,0,r0,c13,c0,3
 *
 * Note what _fleh_swi does NOT do: it never looks at the SWI immediate. The
 * syscall number arrives in r12 ("CMN ip,#3" is its first instruction) and
 * _unix_syscall re-reads it from the saved frame at +0x30, which is r12's slot
 * in the STM^ above. The arguments come from the saved r0-r6 (it computes
 * "&regs->r[0]" or "&regs->r[1]" and rejects anything with more than 7 args) —
 * never from the user stack. So the whole syscall ABI rests on the `^` block
 * transfers below reading and writing the USER bank.
 */

/* Put the core in User mode at `pc` with the MMU off and a program loaded at 0. */
static void user_mode_at(arm_cpu_t *c, const uint32_t *prog, size_t words,
                         uint32_t pc) {
    memset(g_ram, 0, sizeof g_ram);
    for (size_t i = 0; i < words; i++) m_w32(NULL, (uint32_t)(i * 4), prog[i]);
    arm_reset(c, &g_bus);
    arm_set_mode(c, ARM_MODE_USR);
    c->cpsr &= ~(ARM_CPSR_I | ARM_CPSR_F | ARM_CPSR_A);
    c->r[15] = pc;
}

static arm_bus_t bus_with_svc_probe(svc_probe_t *probe) {
    arm_bus_t bus = g_bus;
    arm_bus_set_privileged_svc_handler(&bus, probe_privileged_svc, probe);
    return bus;
}

static void test_privileged_svc_hook_handles_a32(void) {
    const uint32_t svc = 0xefabc123u;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x100u, svc);

    svc_probe_t probe = {0};
    probe.result = ARM_SVC_HANDLED;
    probe.mutate = true;
    arm_bus_t bus = bus_with_svc_probe(&probe);
    arm_cpu_t c;
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_SYS);
    c.cpsr = ARM_MODE_SYS | ARM_CPSR_Z | ARM_CPSR_C;
    c.r[0] = 0x12345678u;
    c.r[7] = 0x77777777u;
    c.r[15] = 0x100u;
    c.cp15.context_id = 0x13572468u;
    c.excl_valid = true;
    c.excl_addr = 0x400u;
    c.spsr[ARM_BANK_SVC] = 0xa5a5a5a5u;
    c.bank_r14[ARM_BANK_SVC] = 0x5a5a5a5au;
    probe.expected_cpu = &c;

    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_OK && probe.calls == 1u,
          "status=%d calls=%u expect handled OK/1", (int)st, probe.calls);
    CHECK(probe.saw_expected_cpu, "hook did not receive the live CPU object");
    CHECK(probe.seen_pc == 0x100u && probe.seen_cpu_pc == 0x100u,
          "pc arg=%08x cpu.pc=%08x expect exact current PC 00000100",
          probe.seen_pc, probe.seen_cpu_pc);
    CHECK(probe.seen_encoding == svc,
          "encoding=%08x expect raw A32 word %08x", probe.seen_encoding, svc);
    CHECK(probe.seen_cpsr == (ARM_MODE_SYS | ARM_CPSR_Z | ARM_CPSR_C),
          "cpsr seen=%08x expect pre-exception SYS state", probe.seen_cpsr);
    CHECK(probe.seen_r0 == 0x12345678u,
          "r0 seen=%08x expect complete pre-hook register state", probe.seen_r0);
    CHECK(c.r[15] == 0x104u && c.cycles == 1u,
          "pc=%08x cycles=%llu expect sequentially retired A32 SVC",
          c.r[15], (unsigned long long)c.cycles);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SYS &&
          (c.cpsr & ARM_CPSR_N) != 0u,
          "handled SVC must stay in SYS and commit hook CPSR changes (%08x)",
          c.cpsr);
    CHECK(c.r[0] == 0xfeed0001u && c.r[7] == 0xfeed0007u &&
          c.cp15.context_id == 0xfeedc013u,
          "handled hook register writes were not committed");
    CHECK(c.spsr[ARM_BANK_SVC] == 0xa5a5a5a5u &&
          c.bank_r14[ARM_BANK_SVC] == 0x5a5a5a5au,
          "handled host service must not enter or alter the SVC exception bank");
}

static void test_privileged_svc_hook_redirects_a32_to_thumb(void) {
    const uint32_t svc = 0xefabc123u;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0x100u, svc);
    m_w16(NULL, 0x202u, 0x225au);             /* MOVS r2,#0x5a (Thumb) */

    svc_probe_t probe = {0};
    probe.result = ARM_SVC_REDIRECTED;
    probe.mutate = true;
    probe.redirect = true;
    probe.redirect_thumb = true;
    probe.redirect_pc = 0x202u;
    arm_bus_t bus = bus_with_svc_probe(&probe);
    arm_cpu_t c;
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_SYS);
    c.cpsr = ARM_MODE_SYS | ARM_CPSR_Z | ARM_CPSR_C;
    c.r[0] = 0x12345678u;
    c.r[7] = 0x77777777u;
    c.r[15] = 0x100u;
    c.cp15.context_id = 0x13572468u;
    c.excl_valid = true;
    c.excl_addr = 0x400u;
    c.spsr[ARM_BANK_SVC] = 0xa5a5a5a5u;
    c.bank_r14[ARM_BANK_SVC] = 0x5a5a5a5au;
    c.cycles = 40u;
    probe.expected_cpu = &c;

    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_OK && probe.calls == 1u && probe.saw_expected_cpu,
          "A32 redirect status=%d calls=%u", (int)st, probe.calls);
    CHECK(probe.seen_pc == 0x100u && probe.seen_cpu_pc == 0x100u &&
          probe.seen_encoding == svc,
          "A32 redirect hook saw pc=%08x cpu.pc=%08x encoding=%08x",
          probe.seen_pc, probe.seen_cpu_pc, probe.seen_encoding);
    CHECK(c.r[15] == 0x202u && (c.cpsr & ARM_CPSR_T) != 0u &&
          c.cycles == 41u,
          "A32 redirect pc=%08x cpsr=%08x cycles=%llu expect Thumb 202/41",
          c.r[15], c.cpsr, (unsigned long long)c.cycles);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SYS &&
          (c.cpsr & ARM_CPSR_N) != 0u &&
          c.r[0] == 0xfeed0001u && c.r[7] == 0xfeed0007u &&
          c.cp15.context_id == 0xfeedc013u &&
          !c.excl_valid && c.excl_addr == 0xfeedeeeeu,
          "A32 redirect did not commit the callback's complete CPU mutation");
    CHECK(c.spsr[ARM_BANK_SVC] == 0xa5a5a5a5u &&
          c.bank_r14[ARM_BANK_SVC] == 0x5a5a5a5au,
          "A32 redirect incorrectly entered or altered the SVC bank");

    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[2] == 0x5au && c.r[15] == 0x204u &&
          (c.cpsr & ARM_CPSR_T) != 0u && c.cycles == 42u &&
          probe.calls == 1u,
          "redirect target did not fetch as Thumb: status=%d r2=%08x pc=%08x "
          "cpsr=%08x cycles=%llu calls=%u",
          (int)st, c.r[2], c.r[15], c.cpsr,
          (unsigned long long)c.cycles, probe.calls);
}

static void test_privileged_svc_hook_nonhandled_is_transactional(void) {
    static const arm_svc_result_t results[] = {
        ARM_SVC_UNHANDLED,
        (arm_svc_result_t)7
    };
    const uint32_t svc = 0xef765432u;

    for (size_t i = 0; i < sizeof results / sizeof results[0]; i++) {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, svc);
        svc_probe_t probe = {0};
        probe.result = results[i];
        probe.mutate = true;
        arm_bus_t bus = bus_with_svc_probe(&probe);
        arm_cpu_t c;
        arm_reset(&c, &bus);
        arm_set_mode(&c, ARM_MODE_IRQ);
        const uint32_t old_cpsr = ARM_MODE_IRQ | ARM_CPSR_C;
        c.cpsr = old_cpsr;
        c.r[0] = 0x01020304u;
        c.r[7] = 0x07070707u;
        c.cp15.context_id = 0x89abcdefu;
        c.excl_valid = true;
        c.excl_addr = 0x400u;
        probe.expected_cpu = &c;

        arm_status_t st = arm_step(&c);
        CHECK(st == ARM_OK && probe.calls == 1u,
              "case %u status=%d calls=%u expect callback then guest SVC",
              (unsigned)i, (int)st, probe.calls);
        CHECK(probe.saw_expected_cpu && probe.seen_pc == 0u &&
              probe.seen_cpu_pc == 0u && probe.seen_encoding == svc,
              "case %u callback did not receive exact CPU/PC/encoding",
              (unsigned)i);
        CHECK(c.r[15] == ARM_VEC_SWI &&
              (c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC && c.r[14] == 4u &&
              c.cycles == 1u,
              "case %u did not fail closed through the ordinary SVC vector",
              (unsigned)i);
        CHECK(c.spsr[ARM_BANK_SVC] == old_cpsr,
              "case %u SPSR=%08x expect pristine pre-hook CPSR %08x",
              (unsigned)i, c.spsr[ARM_BANK_SVC], old_cpsr);
        CHECK(c.r[0] == 0x01020304u && c.r[7] == 0x07070707u &&
              c.cp15.context_id == 0x89abcdefu,
              "case %u leaked a declined/unknown hook's CPU mutations",
              (unsigned)i);
        CHECK(!c.excl_valid,
              "case %u must still clear the monitor on ordinary exception entry",
              (unsigned)i);
    }
}

static void test_privileged_svc_hook_a32_error_halts_transactionally(void) {
    const uint32_t svc = 0xef2468acu;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, svc);
    svc_probe_t probe = {0};
    probe.result = ARM_SVC_ERROR;
    probe.mutate = true;
    arm_bus_t bus = bus_with_svc_probe(&probe);
    arm_cpu_t c;
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_IRQ);
    const uint32_t old_cpsr = ARM_MODE_IRQ | ARM_CPSR_C;
    c.cpsr = old_cpsr;
    c.r[0] = 0x01020304u;
    c.r[7] = 0x07070707u;
    c.r[14] = 0x14141414u;
    c.cp15.context_id = 0x89abcdefu;
    c.excl_valid = true;
    c.excl_addr = 0x400u;
    c.spsr[ARM_BANK_SVC] = 0xa5a5a5a5u;
    c.bank_r14[ARM_BANK_SVC] = 0x51515151u;
    c.cycles = UINT64_MAX;              /* increment wraps; ERROR must undo it */
    probe.expected_cpu = &c;

    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_HALT && probe.calls == 1u,
          "A32 ERROR status=%d calls=%u expect HALT/1", (int)st, probe.calls);
    CHECK(probe.saw_expected_cpu && probe.seen_pc == 0u &&
          probe.seen_cpu_pc == 0u && probe.seen_encoding == svc,
          "A32 ERROR callback did not receive exact CPU/PC/encoding");
    CHECK(c.r[15] == 0u && c.cycles == UINT64_MAX && c.cpsr == old_cpsr,
          "A32 ERROR pc=%08x cycles=%llu cpsr=%08x must not retire the SVC",
          c.r[15], (unsigned long long)c.cycles, c.cpsr);
    CHECK(c.r[0] == 0x01020304u && c.r[7] == 0x07070707u &&
          c.r[14] == 0x14141414u && c.cp15.context_id == 0x89abcdefu,
          "A32 ERROR leaked callback register mutations");
    CHECK(c.excl_valid && c.excl_addr == 0x400u,
          "A32 ERROR must restore the pre-hook exclusive monitor");
    CHECK(c.spsr[ARM_BANK_SVC] == 0xa5a5a5a5u &&
          c.bank_r14[ARM_BANK_SVC] == 0x51515151u,
          "A32 ERROR must not enter or alter the guest SVC exception bank");
}

static void test_privileged_svc_hook_obeys_a32_guards(void) {
    svc_probe_t probe = {0};
    probe.result = ARM_SVC_HANDLED;
    arm_bus_t bus = bus_with_svc_probe(&probe);

    /* User SVC is always owned by the guest, even when a host callback says it
     * would handle that encoding. */
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xef000080u);
    arm_cpu_t c;
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_USR);
    c.cpsr = ARM_MODE_USR;
    probe.expected_cpu = &c;
    arm_step(&c);
    CHECK(probe.calls == 0u, "user-mode A32 SVC called the privileged hook");
    CHECK(c.r[15] == ARM_VEC_SWI &&
          (c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC && c.r[14] == 4u,
          "user A32 SVC must retain the ordinary guest exception path");

    /* EQ SVC with Z clear is a condition-failed instruction, not a service
     * request. It retires without either callback or exception. */
    probe.calls = 0;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0x0f13579bu);
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_SYS);
    c.cpsr = ARM_MODE_SYS;                    /* Z clear: EQ fails */
    probe.expected_cpu = &c;
    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_OK && probe.calls == 0u && c.r[15] == 4u,
          "condition-failed SVC status=%d calls=%u pc=%08x expect OK/0/4",
          (int)st, probe.calls, c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SYS,
          "condition-failed SVC incorrectly entered mode %02x",
          c.cpsr & ARM_CPSR_MODE_MASK);

    /* Clearing the bus hook must clear its independent context too and restore
     * the byte-for-byte default privileged SVC path. */
    arm_bus_set_privileged_svc_handler(&bus, NULL, &probe);
    CHECK(bus.privileged_svc_handler == NULL && bus.privileged_svc_ctx == NULL,
          "clearing the SVC hook left a handler or dangling context");
    probe.calls = 0;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xef000000u);
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_SYS);
    c.cpsr = ARM_MODE_SYS;
    st = arm_step(&c);
    CHECK(st == ARM_OK && probe.calls == 0u && c.r[15] == ARM_VEC_SWI &&
          (c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC,
          "cleared hook did not restore the ordinary privileged SVC path");
}

static void test_privileged_svc_hook_handles_thumb(void) {
    const uint16_t svc = 0xdfa5u;
    memset(g_ram, 0, sizeof g_ram);
    m_w16(NULL, 0x100u, svc);
    svc_probe_t probe = {0};
    probe.result = ARM_SVC_HANDLED;
    probe.mutate = true;
    arm_bus_t bus = bus_with_svc_probe(&probe);
    arm_cpu_t c;
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_SYS);
    c.cpsr = ARM_MODE_SYS | ARM_CPSR_T | ARM_CPSR_Z;
    c.r[0] = 0x2468ace0u;
    c.r[15] = 0x100u;
    c.spsr[ARM_BANK_SVC] = 0x55aa55aau;
    probe.expected_cpu = &c;

    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_OK && probe.calls == 1u && probe.saw_expected_cpu,
          "Thumb handled status=%d calls=%u", (int)st, probe.calls);
    CHECK(probe.seen_pc == 0x100u && probe.seen_cpu_pc == 0x100u &&
          probe.seen_encoding == (uint32_t)svc,
          "Thumb hook saw pc=%08x cpu.pc=%08x encoding=%08x",
          probe.seen_pc, probe.seen_cpu_pc, probe.seen_encoding);
    CHECK((probe.seen_cpsr & (ARM_CPSR_MODE_MASK | ARM_CPSR_T)) ==
          (ARM_MODE_SYS | ARM_CPSR_T) && probe.seen_r0 == 0x2468ace0u,
          "Thumb hook did not see the exact ISA/mode/register state");
    CHECK(c.r[15] == 0x102u && c.cycles == 1u &&
          (c.cpsr & (ARM_CPSR_MODE_MASK | ARM_CPSR_T)) ==
          (ARM_MODE_SYS | ARM_CPSR_T),
          "handled Thumb SVC did not retire sequentially in Thumb/SYS");
    CHECK(c.r[0] == 0xfeed0001u && c.cp15.context_id == 0xfeedc013u,
          "handled Thumb hook writes were not committed");
    CHECK(c.spsr[ARM_BANK_SVC] == 0x55aa55aau,
          "handled Thumb SVC incorrectly entered the SVC exception bank");
}

static void test_privileged_svc_hook_redirects_thumb_to_a32(void) {
    const uint16_t svc = 0xdfa5u;
    memset(g_ram, 0, sizeof g_ram);
    m_w16(NULL, 0x100u, svc);
    m_w32(NULL, 0x200u, 0xe3a02066u);         /* MOV r2,#0x66 (A32) */

    svc_probe_t probe = {0};
    probe.result = ARM_SVC_REDIRECTED;
    probe.mutate = true;
    probe.redirect = true;
    probe.redirect_thumb = false;
    probe.redirect_pc = 0x200u;
    arm_bus_t bus = bus_with_svc_probe(&probe);
    arm_cpu_t c;
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_SYS);
    c.cpsr = ARM_MODE_SYS | ARM_CPSR_T | ARM_CPSR_C;
    c.r[0] = 0x2468ace0u;
    c.r[15] = 0x100u;
    c.spsr[ARM_BANK_SVC] = 0x55aa55aau;
    c.cycles = 90u;
    probe.expected_cpu = &c;

    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_OK && probe.calls == 1u && probe.saw_expected_cpu,
          "Thumb redirect status=%d calls=%u", (int)st, probe.calls);
    CHECK(probe.seen_pc == 0x100u && probe.seen_cpu_pc == 0x100u &&
          probe.seen_encoding == (uint32_t)svc &&
          (probe.seen_cpsr & ARM_CPSR_T) != 0u,
          "Thumb redirect hook saw pc=%08x cpu.pc=%08x encoding=%08x cpsr=%08x",
          probe.seen_pc, probe.seen_cpu_pc, probe.seen_encoding, probe.seen_cpsr);
    CHECK(c.r[15] == 0x200u && (c.cpsr & ARM_CPSR_T) == 0u &&
          c.cycles == 91u,
          "Thumb redirect pc=%08x cpsr=%08x cycles=%llu expect A32 200/91",
          c.r[15], c.cpsr, (unsigned long long)c.cycles);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SYS &&
          (c.cpsr & ARM_CPSR_N) != 0u &&
          c.r[0] == 0xfeed0001u && c.r[7] == 0xfeed0007u &&
          c.cp15.context_id == 0xfeedc013u &&
          !c.excl_valid && c.excl_addr == 0xfeedeeeeu,
          "Thumb redirect did not commit the callback's complete CPU mutation");
    CHECK(c.spsr[ARM_BANK_SVC] == 0x55aa55aau,
          "Thumb redirect incorrectly entered the SVC exception bank");

    st = arm_step(&c);
    CHECK(st == ARM_OK && c.r[2] == 0x66u && c.r[15] == 0x204u &&
          (c.cpsr & ARM_CPSR_T) == 0u && c.cycles == 92u &&
          probe.calls == 1u,
          "redirect target did not fetch as A32: status=%d r2=%08x pc=%08x "
          "cpsr=%08x cycles=%llu calls=%u",
          (int)st, c.r[2], c.r[15], c.cpsr,
          (unsigned long long)c.cycles, probe.calls);
}

static void test_privileged_svc_hook_thumb_error_and_user_guard(void) {
    const uint16_t svc = 0xdf3cu;
    svc_probe_t probe = {0};
    probe.result = ARM_SVC_ERROR;
    probe.mutate = true;
    arm_bus_t bus = bus_with_svc_probe(&probe);

    memset(g_ram, 0, sizeof g_ram);
    m_w16(NULL, 0x100u, svc);
    arm_cpu_t c;
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_SYS);
    const uint32_t old_cpsr = ARM_MODE_SYS | ARM_CPSR_T | ARM_CPSR_C;
    c.cpsr = old_cpsr;
    c.r[0] = 0x11223344u;
    c.r[7] = 0x55667788u;
    c.r[14] = 0x14141414u;
    c.r[15] = 0x100u;
    c.cp15.context_id = 0x10203040u;
    c.excl_valid = true;
    c.excl_addr = 0x400u;
    c.spsr[ARM_BANK_SVC] = 0xa5a5a5a5u;
    c.bank_r14[ARM_BANK_SVC] = 0x51515151u;
    c.cycles = UINT64_MAX;              /* increment wraps; ERROR must undo it */
    probe.expected_cpu = &c;
    arm_status_t st = arm_step(&c);
    CHECK(st == ARM_HALT && probe.calls == 1u &&
          probe.seen_encoding == (uint32_t)svc,
          "Thumb ERROR status=%d calls=%u encoding=%08x expect HALT/1/%04x",
          (int)st, probe.calls, probe.seen_encoding, svc);
    CHECK(c.r[15] == 0x100u && c.cycles == UINT64_MAX && c.cpsr == old_cpsr,
          "Thumb ERROR pc=%08x cycles=%llu cpsr=%08x must not retire the SVC",
          c.r[15], (unsigned long long)c.cycles, c.cpsr);
    CHECK(c.r[0] == 0x11223344u && c.r[7] == 0x55667788u &&
          c.r[14] == 0x14141414u && c.cp15.context_id == 0x10203040u,
          "Thumb ERROR leaked callback register mutations");
    CHECK(c.excl_valid && c.excl_addr == 0x400u,
          "Thumb ERROR must restore the pre-hook exclusive monitor");
    CHECK(c.spsr[ARM_BANK_SVC] == 0xa5a5a5a5u &&
          c.bank_r14[ARM_BANK_SVC] == 0x51515151u,
          "Thumb ERROR must not enter or alter the guest SVC exception bank");

    /* Thumb has no condition field, but it has the same hard User-mode gate. */
    memset(g_ram, 0, sizeof g_ram);
    m_w16(NULL, 0x100u, svc);
    probe.calls = 0;
    probe.result = ARM_SVC_HANDLED;
    arm_reset(&c, &bus);
    arm_set_mode(&c, ARM_MODE_USR);
    c.cpsr = ARM_MODE_USR | ARM_CPSR_T;
    c.r[15] = 0x100u;
    probe.expected_cpu = &c;
    st = arm_step(&c);
    CHECK(probe.calls == 0u, "user-mode Thumb SVC called the privileged hook");
    CHECK(st == ARM_OK && c.r[15] == ARM_VEC_SWI && c.r[14] == 0x102u &&
          (c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC,
          "user Thumb SVC must retain the ordinary guest exception path");
}

static void test_swi_entry_state_from_user_mode(void) {
    /* The full ARM ARM (ARMv6, A2.6.4) entry contract for SWI, taken from User
     * mode — the only mode it will ever be taken from once launchd runs. */
    uint32_t p[0x44] = {0};
    p[0x40] = 0xef000080u;                    /* 0x100: SWI #0x80 */
    arm_cpu_t c; user_mode_at(&c, p, 0x44, 0x100);
    arm_set_mode(&c, ARM_MODE_SVC);
    c.r[13] = 0x900; c.r[14] = 0xdeadbeefu;   /* SVC bank, must be overwritten */
    arm_set_mode(&c, ARM_MODE_USR);
    c.r[13] = 0xaaaa0000u; c.r[14] = 0xbbbb0000u;
    c.r[12] = 4;                              /* the syscall number XNU reads */
    arm_step(&c);

    CHECK(c.r[15] == ARM_VEC_SWI, "pc=%08x expect 08", c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_SVC,
          "mode=%02x expect SVC", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(c.r[14] == 0x104, "lr_svc=%08x expect 104 (the instruction AFTER the "
          "SWI; _unix_syscall rewinds this by 4 for ERESTART)", c.r[14]);
    CHECK(c.r[13] == 0x900, "sp_svc=%08x expect the SVC bank, not the user's", c.r[13]);
    CHECK(c.spsr[ARM_BANK_SVC] == (ARM_MODE_USR),
          "spsr_svc=%08x expect the pre-switch User CPSR", c.spsr[ARM_BANK_SVC]);
    CHECK((c.cpsr & ARM_CPSR_I) != 0, "I must be set on entry");
    CHECK((c.cpsr & ARM_CPSR_A) == 0,
          "A must NOT be set: SWI and Undefined are the two vectors that leave "
          "it alone (A2.6). Setting it here would put a bit in every SPSR XNU "
          "saves that hardware would not have put there");
    CHECK((c.cpsr & ARM_CPSR_T) == 0, "T must be cleared: handlers run in ARM state");
    CHECK((c.cpsr & ARM_CPSR_E) == 0, "E must come from SCTLR.EE, which is clear");
    CHECK(c.r[12] == 4, "r12 is not banked and must survive the switch to SVC");
}

static void test_thumb_swi_lr_is_the_next_halfword(void) {
    /* In Thumb state the SWI is two bytes, so the return address is PC+2, not
     * PC+4. Getting this wrong resumes the process one instruction early. */
    uint32_t p[0x44] = {0};
    m_w32(NULL, 0, 0);
    arm_cpu_t c; user_mode_at(&c, p, 0x44, 0x100);
    m_w16(NULL, 0x100, 0xdf80u);              /* SWI #0x80, Thumb */
    c.cpsr |= ARM_CPSR_T;
    arm_step(&c);

    CHECK(c.r[15] == ARM_VEC_SWI, "pc=%08x expect 08", c.r[15]);
    CHECK(c.r[14] == 0x102, "lr_svc=%08x expect 102 (PC+2 in Thumb)", c.r[14]);
    CHECK((c.cpsr & ARM_CPSR_T) == 0, "the handler runs in ARM state");
    CHECK((c.spsr[ARM_BANK_SVC] & ARM_CPSR_T) != 0,
          "SPSR.T must record that the caller was Thumb — _fleh_undef and "
          "_sleh_undef key off exactly this bit");
    CHECK((c.spsr[ARM_BANK_SVC] & ARM_CPSR_MODE_MASK) == ARM_MODE_USR,
          "SPSR mode must be User; _fleh_* branch on \"TST spsr,#0xf\"");
}

static void test_irq_sets_a_but_swi_does_not(void) {
    /* The distinction take_exception draws, pinned from both sides so neither
     * half can regress alone. */
    uint32_t p[] = { 0xef000000u };           /* SWI #0 */
    arm_cpu_t c; user_mode_at(&c, p, 1, 0);
    arm_step(&c);
    CHECK((c.cpsr & ARM_CPSR_A) == 0, "SWI must leave A alone");

    arm_cpu_t d; user_mode_at(&d, p, 1, 0);
    d.irq_line = true;
    arm_step(&d);
    CHECK(d.r[15] == ARM_VEC_IRQ, "pc=%08x expect 18 (IRQ)", d.r[15]);
    CHECK((d.cpsr & ARM_CPSR_A) != 0, "IRQ entry must set A");
    CHECK((d.cpsr & ARM_CPSR_F) == 0, "IRQ entry must NOT mask FIQ");
}

static void test_exception_entry_takes_cpsr_e_from_sctlr_ee(void) {
    /* CPSR.E <- SCTLR.EE, not "E is preserved". We never set EE, so the bit
     * must come back clear even when the interrupted code had set it. */
    uint32_t p[] = { 0xe3a00c02u,   /* MOV r0,#0x200  (the E bit)  */
                     0xe122f000u,   /* MSR CPSR_x, r0 -> sets E    */
                     0xef000000u }; /* SWI #0                      */
    arm_cpu_t c; load_and_run(&c, p, 3, 2);
    CHECK((c.cpsr & ARM_CPSR_E) != 0, "MSR should have set E for this test");
    arm_step(&c);
    CHECK((c.cpsr & ARM_CPSR_E) == 0, "exception entry must clear E from SCTLR.EE");
    CHECK((c.spsr[ARM_BANK_SVC] & ARM_CPSR_E) != 0,
          "the SPSR must still record that the caller had E set");
}

static void test_xnu_syscall_frame_round_trip(void) {
    /*
     * The real thing: SWI from User mode, _fleh_swi's "STM sp,{r0-r14}^",
     * the handler scribbling on the registers, _thread_exception_return's
     * "LDM sp,{r0-r14}^" and "MOVS pc,lr". Every user register must come back,
     * the SVC bank must be untouched throughout, and the frame in memory must
     * hold the USER r13/r14 rather than the handler's.
     */
    uint32_t p[0x44] = {0};
    p[0x02] = 0xe8cd7fffu;   /* 0x08: STM sp,{r0-r14}^   (the SWI vector) */
    p[0x03] = 0xe1a00000u;   /* 0x0c: NOP (the mandatory gap after ^)     */
    p[0x04] = 0xe3a00000u;   /* 0x10: MOV r0,#0   — clobber               */
    p[0x05] = 0xe3a0c000u;   /* 0x14: MOV r12,#0  — clobber               */
    p[0x06] = 0xe8dd7fffu;   /* 0x18: LDM sp,{r0-r14}^                    */
    p[0x07] = 0xe1a00000u;   /* 0x1c: NOP                                 */
    p[0x08] = 0xe1b0f00eu;   /* 0x20: MOVS pc,lr                          */
    p[0x40] = 0xef000000u;   /* 0x100: SWI #0                             */

    arm_cpu_t c; user_mode_at(&c, p, 0x44, 0x100);
    arm_set_mode(&c, ARM_MODE_SVC);
    c.r[13] = 0x900;                       /* the per-thread save area   */
    c.r[14] = 0xcccc0000u;                 /* SVC LR, must not leak out  */
    arm_set_mode(&c, ARM_MODE_USR);
    for (unsigned i = 0; i < 13; i++) c.r[i] = 0x1000u + i;
    c.r[13] = 0xaaaa0000u; c.r[14] = 0xbbbb0000u;

    for (int i = 0; i < 8; i++) arm_step(&c);

    /* The frame the kernel reads: r12 at +0x30, user sp at +0x34, user lr +0x38. */
    CHECK(m_r32(NULL, 0x900 + 0x00) == 0x1000u, "frame r0=%08x", m_r32(NULL, 0x900));
    CHECK(m_r32(NULL, 0x900 + 0x30) == 0x100cu,
          "frame+0x30=%08x expect r12 — this is the slot _unix_syscall reads the "
          "syscall number from", m_r32(NULL, 0x900 + 0x30));
    CHECK(m_r32(NULL, 0x900 + 0x34) == 0xaaaa0000u,
          "frame+0x34=%08x expect the USER sp, not sp_svc", m_r32(NULL, 0x900 + 0x34));
    CHECK(m_r32(NULL, 0x900 + 0x38) == 0xbbbb0000u,
          "frame+0x38=%08x expect the USER lr", m_r32(NULL, 0x900 + 0x38));

    /* And the return. */
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR,
          "mode=%02x expect back in User", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(c.r[15] == 0x104, "pc=%08x expect 104", c.r[15]);
    CHECK(c.r[0] == 0x1000u && c.r[12] == 0x100cu,
          "r0=%08x r12=%08x: the clobbered registers must be restored",
          c.r[0], c.r[12]);
    CHECK(c.r[13] == 0xaaaa0000u && c.r[14] == 0xbbbb0000u,
          "sp=%08x lr=%08x expect the user bank back", c.r[13], c.r[14]);
    arm_set_mode(&c, ARM_MODE_SVC);
    CHECK(c.r[13] == 0x900 && c.r[14] == 0x104,
          "sp_svc=%08x lr_svc=%08x: the LDM^ must write the USER bank ONLY. "
          "lr_svc still has to hold the resume address the SWI put there — "
          "_thread_exception_return reloads it from the frame BEFORE the LDM^ "
          "and then feeds it straight to MOVS pc,lr, so a LDM^ that wrote the "
          "current bank would return to the user's r14 instead", c.r[13], c.r[14]);
}

static void test_syscall_error_flag_reaches_user_through_the_spsr(void) {
    /*
     * How a failed syscall tells the process. _unix_syscall does
     * "LDR r3,[r6,#0x40]; ORR r3,r3,#0x20000000; STR r3,[r6,#0x40]" — it sets
     * the C flag in the SAVED CPSR — and _thread_exception_return reloads that
     * word into SPSR_svc with "MSR spsr_fsxc,r4" before "MOVS pc,lr". So the
     * flags a user process sees after a syscall travel entirely through the
     * SPSR: an exception return that recomputed NZCV from its own arithmetic,
     * or that restored only the control byte, would make every syscall look
     * successful to libSystem's "bcs cerror".
     */
    uint32_t p[] = { 0xe16ff000u,   /* 0x00: MSR spsr_fsxc, r0 */
                     0xe1b0f00eu }; /* 0x04: MOVS pc, lr       */
    arm_cpu_t c; arm_reset(&c, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, p[0]); m_w32(NULL, 4, p[1]);
    arm_set_mode(&c, ARM_MODE_SVC);
    c.r[0]  = ARM_MODE_USR | ARM_CPSR_C;   /* the frame's cpsr word, C set */
    c.r[14] = 0x00001020u;
    c.cpsr |= ARM_CPSR_Z;                  /* handler flags, must not survive */
    c.cpsr &= ~ARM_CPSR_C;
    c.r[15] = 0;
    arm_step(&c); arm_step(&c);

    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR,
          "mode=%02x expect User", c.cpsr & ARM_CPSR_MODE_MASK);
    CHECK((c.cpsr & ARM_CPSR_C) != 0,
          "cpsr=%08x: C must arrive from the SPSR — this is the syscall error "
          "indication", c.cpsr);
    CHECK((c.cpsr & ARM_CPSR_Z) == 0,
          "cpsr=%08x: the handler's own flags must not leak into user mode", c.cpsr);
    CHECK(c.r[15] == 0x00001020u, "pc=%08x expect 1020", c.r[15]);
}

static void test_user_bank_ldm_writes_the_user_bank(void) {
    /* The load direction of the same rule, isolated. */
    arm_cpu_t c; arm_reset(&c, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    arm_set_mode(&c, ARM_MODE_USR);
    c.r[13] = 0x1111; c.r[14] = 0x2222;
    arm_set_mode(&c, ARM_MODE_SVC);
    c.r[13] = 0x800; c.r[14] = 0xcccc0000u;
    m_w32(NULL, 0x800, 0x33330000u);      /* -> user r13 */
    m_w32(NULL, 0x804, 0x44440000u);      /* -> user r14 */
    m_w32(NULL, 0, 0xe8dd6000u);          /* LDM sp,{r13,r14}^ */
    c.r[15] = 0;
    arm_step(&c);

    CHECK(c.r[13] == 0x800 && c.r[14] == 0xcccc0000u,
          "sp_svc=%08x lr_svc=%08x must be untouched", c.r[13], c.r[14]);
    arm_set_mode(&c, ARM_MODE_USR);
    CHECK(c.r[13] == 0x33330000u && c.r[14] == 0x44440000u,
          "user sp=%08x lr=%08x expect the loaded values", c.r[13], c.r[14]);
}

static void test_user_bank_stm_from_fiq_uses_the_user_r8_r12(void) {
    /* FIQ is the mode where the `^` form has the most to get wrong: it banks
     * r8-r12 as well as r13/r14, so a save that reads the live registers would
     * write FIQ's private copies into the interrupted thread's frame and hand
     * them back to it on resume. */
    arm_cpu_t c; arm_reset(&c, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    arm_set_mode(&c, ARM_MODE_USR);
    for (unsigned i = 8; i <= 12; i++) c.r[i] = 0xd0000000u + i;
    c.r[13] = 0xaaaa0000u; c.r[14] = 0xbbbb0000u;
    arm_set_mode(&c, ARM_MODE_FIQ);
    for (unsigned i = 8; i <= 12; i++) c.r[i] = 0xf0000000u + i;
    c.r[13] = 0x800; c.r[14] = 0xcccc0000u;
    m_w32(NULL, 0, 0xe8cd7f00u);          /* STMIA sp,{r8-r14}^ */
    c.r[15] = 0;
    arm_step(&c);

    for (unsigned i = 0; i < 5; i++)
        CHECK(m_r32(NULL, 0x800 + i * 4) == 0xd0000000u + 8 + i,
              "frame r%u=%08x expect the USER bank, not FIQ's", 8 + i,
              m_r32(NULL, 0x800 + i * 4));
    CHECK(m_r32(NULL, 0x814) == 0xaaaa0000u, "frame sp=%08x expect user sp",
          m_r32(NULL, 0x814));
    CHECK(m_r32(NULL, 0x818) == 0xbbbb0000u, "frame lr=%08x expect user lr",
          m_r32(NULL, 0x818));
    CHECK(c.r[8] == 0xf0000008u && c.r[14] == 0xcccc0000u,
          "the FIQ bank must be untouched");
}

static void test_user_bank_transfer_with_writeback_traps(void) {
    /* LDM(2)/STM(2) forbid writeback (ARM ARM A4.1.21/A4.1.42): W==1 is
     * UNPREDICTABLE, and the two plausible answers differ exactly when Rn is
     * r13 — the only base XNU uses with this form. Trap rather than pick one. */
    uint32_t st[] = { 0xe8ed7fffu };      /* STMIA sp!,{r0-r14}^ */
    uint32_t ld[] = { 0xe8fd7fffu };      /* LDMIA sp!,{r0-r14}^ */
    arm_cpu_t c;
    CHECK(run_status(&c, st, 1, 1) == ARM_UNDEFINED, "STM^ with writeback must trap");
    CHECK(run_status(&c, ld, 1, 1) == ARM_UNDEFINED, "LDM^ with writeback must trap");
    /* ...but the exception-return LDM, which legitimately writes back, must not. */
    arm_cpu_t d;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xe8fd8000u);          /* LDMIA sp!,{pc}^ */
    m_w32(NULL, 0x800, 0x00001020u);
    arm_reset(&d, &g_bus);
    arm_set_mode(&d, ARM_MODE_SVC);
    d.spsr[ARM_BANK_SVC] = ARM_MODE_USR;
    d.r[13] = 0x800;
    d.r[15] = 0;
    CHECK(arm_step(&d) == ARM_OK,
          "LDM {pc}^ with writeback is LDM(3), where writeback IS allowed");
    CHECK(d.r[15] == 0x00001020u, "pc=%08x expect the return address", d.r[15]);
    CHECK((d.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR,
          "mode=%02x expect User restored from SPSR", d.cpsr & ARM_CPSR_MODE_MASK);
    CHECK(d.bank_r13[ARM_BANK_SVC] == 0x804,
          "sp_svc=%08x expect 804: the writeback lands in the HANDLER's banked "
          "Rn, and only then does CPSR<-SPSR rebank us", d.bank_r13[ARM_BANK_SVC]);
}

static void test_user_bank_transfers_reject_user_and_system_modes(void) {
    static const uint32_t insns[] = {
        0xe8d00002u,                            /* LDMIA r0,{r1}^ */
        0xe8c00002u,                            /* STMIA r0,{r1}^ */
    };
    static const uint32_t modes[] = { ARM_MODE_USR, ARM_MODE_SYS };

    for (unsigned mi = 0; mi < 2u; mi++) {
        for (unsigned ii = 0; ii < 2u; ii++) {
            arm_cpu_t c;
            g_watch_addr = 0xffffffffu;
            g_watch_reads32 = g_watch_writes32 = 0u;
            memset(g_ram, 0, sizeof g_ram);
            m_w32(NULL, 0u, insns[ii]);
            arm_reset(&c, &g_bus);
            arm_set_mode(&c, modes[mi]);
            c.r[0] = 0x800u;
            c.r[1] = 0x11223344u;
            c.r[15] = 0u;
            g_watch_addr = 0x800u;
            CHECK(arm_step(&c) == ARM_UNDEFINED,
                  "%s^ in mode %02x was not refused as UNPREDICTABLE",
                  ii == 0u ? "LDM" : "STM", modes[mi]);
            CHECK(g_watch_reads32 == 0u && g_watch_writes32 == 0u,
                  "%s^ in mode %02x touched memory before rejection",
                  ii == 0u ? "LDM" : "STM", modes[mi]);
        }
    }
    g_watch_addr = 0xffffffffu;
}

static void test_srs_stores_lr_and_spsr_of_the_current_mode(void) {
    /* _fleh_swi+0x18 is "SRSIA sp,#0x13" (0xf8cd0513): it appends the user PC
     * (in lr_svc) and the user CPSR (in spsr_svc) to the frame at +0x3c/+0x40,
     * which is exactly where _thread_exception_return reads them back from. */
    uint32_t p[] = { 0xf8cd0513u };
    arm_cpu_t c; arm_reset(&c, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, p[0]);
    arm_set_mode(&c, ARM_MODE_SVC);
    c.r[13] = 0x93c; c.r[14] = 0x00000104u;
    c.spsr[ARM_BANK_SVC] = ARM_MODE_USR | ARM_CPSR_T;
    c.r[15] = 0;
    arm_step(&c);

    CHECK(m_r32(NULL, 0x93c) == 0x00000104u, "[sp]=%08x expect lr_svc", m_r32(NULL, 0x93c));
    CHECK(m_r32(NULL, 0x940) == (ARM_MODE_USR | ARM_CPSR_T),
          "[sp+4]=%08x expect spsr_svc", m_r32(NULL, 0x940));
    CHECK(c.r[13] == 0x93c, "W==0 in this encoding: sp must not move");
}

static void test_subs_pc_lr_4_returns_from_fiq(void) {
    /* _fleh_fiq_generic ends with "SUBSPL pc,lr,#4" — the third exception-return
     * form, and the only one that arrives at the resume address by arithmetic. */
    uint32_t p[] = { 0xe25ef004u };       /* SUBS pc,lr,#4 */
    arm_cpu_t c; arm_reset(&c, &g_bus);
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, p[0]);
    arm_set_mode(&c, ARM_MODE_FIQ);
    c.spsr[ARM_BANK_FIQ] = ARM_MODE_USR;
    c.r[14] = 0x00001020u;
    c.r[15] = 0;
    arm_step(&c);
    CHECK(c.r[15] == 0x0000101cu, "pc=%08x expect 101c (lr-4)", c.r[15]);
    CHECK((c.cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR,
          "mode=%02x expect User restored from SPSR", c.cpsr & ARM_CPSR_MODE_MASK);
}

/* --- copyin / copyout: the T forms under real user-pmap conditions -------- */

/* Identity-map section 0 as AP=11 (so the fetch works from any mode) and map
 * VA 0x80000000 -> PA 0 as AP=01: privileged RW, user NO ACCESS. That is what
 * a kernel-only page looks like to copyin. */
static void kernel_only_page_setup(arm_cpu_t *c, const uint32_t *prog, size_t words) {
    const uint32_t l1 = 0x4000;
    memset(g_ram, 0, sizeof g_ram);
    for (size_t i = 0; i < words; i++) m_w32(NULL, (uint32_t)(i * 4), prog[i]);
    m_w32(NULL, l1 + 0,           0x00000000u | (3u << 10) | 2u);  /* AP=11 */
    m_w32(NULL, l1 + (0x800 << 2), 0x00000000u | (1u << 10) | 2u); /* AP=01 */
    arm_reset(c, &g_bus);
    c->cp15.ttbr0 = l1;
    c->cp15.dacr  = 1u;                       /* domain 0 client: check AP */
    c->cp15.sctlr |= ARM_SCTLR_M;
    arm_set_mode(c, ARM_MODE_SVC);
    c->r[15] = 0;
}

static void test_ldrt_from_svc_translates_as_unprivileged(void) {
    /* copyin's inner loop is "LDRT r3,[r0],#4" issued from SVC with the user
     * pmap in TTBR0. A plain LDR of the same address succeeds; the T form must
     * fault, because that is the whole mechanism protecting the kernel from
     * dereferencing a pointer the process could not have followed itself. */
    uint32_t p[] = { 0xe3a01102u,   /* MOV r1,#0x80000000 */
                     0xe3811c03u,   /* ORR r1,r1,#0x300   */
                     0xe5910000u,   /* LDR  r0,[r1]       (privileged: ok)   */
                     0xe4b12000u }; /* LDRT r2,[r1]       (unprivileged: fault) */
    arm_cpu_t c; kernel_only_page_setup(&c, p, 4);
    m_w32(NULL, 0x300, 0x5a5aa5a5u);
    arm_step(&c); arm_step(&c); arm_step(&c);
    CHECK(c.r[0] == 0x5a5aa5a5u,
          "r0=%08x: a plain LDR from SVC must read the kernel-only page", c.r[0]);
    arm_step(&c);
    CHECK(c.r[15] == ARM_VEC_DATA_ABORT,
          "pc=%08x expect 10: LDRT from a privileged mode translates as "
          "unprivileged and must abort here", c.r[15]);
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_SECTION_PERMISSION,
          "dfsr=%08x expect a section permission fault", c.cp15.dfsr);
    CHECK((c.cp15.dfsr & (1u << 11)) == 0, "WnR must be clear for a read");
    CHECK(c.cp15.dfar == 0x80000300u, "dfar=%08x expect 80000300", c.cp15.dfar);
}

static void test_strt_from_svc_translates_as_unprivileged(void) {
    /* copyout's mirror image, and the one that also has to get DFSR.WnR right. */
    uint32_t p[] = { 0xe3a01102u,   /* MOV r1,#0x80000000 */
                     0xe3811c03u,   /* ORR r1,r1,#0x300   */
                     0xe5810000u,   /* STR  r0,[r1]       (privileged: ok)   */
                     0xe4a12000u }; /* STRT r2,[r1]       (unprivileged: fault) */
    arm_cpu_t c; kernel_only_page_setup(&c, p, 4);
    c.r[0] = 0x11223344u;
    arm_step(&c); arm_step(&c); arm_step(&c);
    CHECK(m_r32(NULL, 0x300) == 0x11223344u, "a plain STR from SVC must land");
    arm_step(&c);
    CHECK(c.r[15] == ARM_VEC_DATA_ABORT, "pc=%08x expect 10 (STRT must abort)", c.r[15]);
    CHECK((c.cp15.dfsr & (1u << 11)) != 0, "dfsr=%08x: WnR must be set for a write",
          c.cp15.dfsr);
}

static void test_user_mode_load_uses_user_permissions(void) {
    /* And the same page reached from genuine User mode, so `priv` is confirmed
     * to come from the CURRENT mode and not only from the instruction form. */
    uint32_t p[] = { 0xe3a01102u,   /* MOV r1,#0x80000000 */
                     0xe5910000u }; /* LDR r0,[r1]        */
    arm_cpu_t c; kernel_only_page_setup(&c, p, 2);
    arm_set_mode(&c, ARM_MODE_USR);
    arm_step(&c); arm_step(&c);
    CHECK(c.r[15] == ARM_VEC_DATA_ABORT,
          "pc=%08x expect 10: a plain LDR in User mode must fault on a "
          "kernel-only page", c.r[15]);
}

/* --- CP15 is privileged ---------------------------------------------------- */

static arm_status_t run_one_in_user(arm_cpu_t *c, uint32_t insn) {
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, insn);
    arm_reset(c, &g_bus);
    arm_set_mode(c, ARM_MODE_USR);
    c->r[15] = 0;
    return arm_step(c);
}

static void test_cp15_is_privileged_from_user_mode(void) {
    /*
     * CP15 is not a free-for-all. Without this check a user process can write
     * TTBR0 and point translation at a page table it owns, or clear SCTLR.M,
     * or make DACR call every domain a manager — and our MMU would obey. Same
     * shape as the CPS-in-User-mode hole: invisible for the entire kernel-only
     * boot, live from launchd's first instruction.
     */
    arm_cpu_t c;
    CHECK(run_one_in_user(&c, 0xee110f10u) == ARM_UNDEFINED, "MRC SCTLR must trap");
    CHECK(run_one_in_user(&c, 0xee010f10u) == ARM_UNDEFINED, "MCR SCTLR must trap");
    CHECK(run_one_in_user(&c, 0xee020f10u) == ARM_UNDEFINED, "MCR TTBR0 must trap");
    CHECK(run_one_in_user(&c, 0xee030f10u) == ARM_UNDEFINED, "MCR DACR must trap");
    CHECK(run_one_in_user(&c, 0xee1d0f90u) == ARM_UNDEFINED, "MRC TPIDRPRW must trap");
    CHECK(run_one_in_user(&c, 0xee110f30u) == ARM_UNDEFINED, "MRC CP14 must trap");
    /* The same accesses from a privileged mode must still work. */
    uint32_t p[] = { 0xee110f10u };
    arm_cpu_t d; load_and_run(&d, p, 1, 1);
    CHECK(d.r[0] == d.cp15.sctlr, "SCTLR must still be readable from SYS mode");
}

static void test_cp15_thread_id_registers_stay_user_accessible(void) {
    /* The three CP15 accesses the ARM1176 does grant User mode. TPIDRURO is the
     * load-bearing one: _thread_set_cthread_self (0xc0061da0) writes it with
     * "MCR p15,0,r0,c13,c0,3" and libSystem reads it back from User mode. */
    arm_cpu_t c;
    CHECK(run_one_in_user(&c, 0xee1d0f70u) == ARM_OK, "MRC TPIDRURO must be allowed");
    CHECK(run_one_in_user(&c, 0xee0d0f70u) == ARM_UNDEFINED,
          "MCR TPIDRURO is READ-only from User mode");
    CHECK(run_one_in_user(&c, 0xee1d0f50u) == ARM_OK, "MRC TPIDRURW must be allowed");
    CHECK(run_one_in_user(&c, 0xee0d0f50u) == ARM_OK, "MCR TPIDRURW must be allowed");
    CHECK(run_one_in_user(&c, 0xee071f9au) == ARM_OK,
          "the c7 barrier/cache operations stay accessible from User mode");

    /* And the value really round-trips through the kernel-side write. */
    arm_cpu_t d;
    memset(g_ram, 0, sizeof g_ram);
    m_w32(NULL, 0, 0xee1d0f70u);            /* MRC p15,0,r0,c13,c0,3 */
    arm_reset(&d, &g_bus);
    d.cp15.tpidruro = 0xfeedface;
    arm_set_mode(&d, ARM_MODE_USR);
    d.r[15] = 0;
    arm_step(&d);
    CHECK(d.r[0] == 0xfeedfaceu, "r0=%08x expect the cthread_self value", d.r[0]);
}

/* --- page-crossing unaligned accesses, and XN --------------------------- */

/*
 * Two 4 KB small pages behind one coarse table, at VA 0x80000000 and
 * 0x80001000, whose physical frames the caller chooses — and chooses to be far
 * apart, because that is the whole point: a walker that translates only the
 * base of a straddling access reads or writes across the *physical* boundary,
 * which is silently the wrong memory whenever the frames are not adjacent.
 * A frame of 0 leaves that page with no second-level descriptor at all.
 * `xn1` sets execute-never (bit 0 of a small-page descriptor) on the second.
 *
 * The low megabyte is identity-mapped by a section so the instruction fetch
 * always succeeds, and SCTLR.XP is set because that is the configuration XNU
 * runs in and the only one in which XN exists.
 */
static void split_setup(arm_cpu_t *c, const uint32_t *prog, size_t words,
                        uint32_t pa0, uint32_t pa1, bool xn1) {
    const uint32_t l1 = 0x4000, l2 = 0x5000;
    memset(g_ram, 0, sizeof g_ram);
    for (size_t i = 0; i < words; i++) m_w32(NULL, (uint32_t)(i * 4), prog[i]);
    m_w32(NULL, l1 + (0x000u << 2), 0x00000000u | (3u << 10) | 8u | 2u);/* Normal identity */
    m_w32(NULL, l1 + (0x800u << 2), (l2 & 0xfffffc00u) | 1u);         /* coarse   */
    if (pa0) m_w32(NULL, l2 + (0u << 2),
                    (pa0 & 0xfffff000u) | (3u << 4) | 8u | 2u);
    if (pa1) m_w32(NULL, l2 + (1u << 2),
                    (pa1 & 0xfffff000u) | (3u << 4) | 8u | 2u | (xn1 ? 1u : 0u));
    arm_reset(c, &g_bus);
    c->cp15.ttbr0  = l1;
    c->cp15.dacr   = 1u;                       /* domain 0: client */
    c->cp15.sctlr |= ARM_SCTLR_M | ARM_SCTLR_XP | ARM_SCTLR_U;
    c->cpsr = (c->cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
    c->r[15] = 0;
}

/*
 * ARMv6 parallel add/subtract. Run32 stopped on 0xe611ef9e, SADD8 lr, r1, lr,
 * in SpringBoard's path to its first frame. The whole family is implemented
 * together because the classes differ only in how a lane result is reduced, so
 * covering one and trapping its neighbours would hide the next stop behind an
 * identical shape.
 *
 * Each case builds r1/r2, executes one encoding, and checks the packed result
 * and the GE flags. Lane independence is the property that matters most: a
 * carry or borrow must never cross a lane boundary.
 */
static void test_parallel_add_sub_family(void) {
    arm_cpu_t c;
    static const struct {
        uint32_t insn, a, b, expect, ge;
        bool check_ge;
        const char *what;
    } CASES[] = {
        /* Every encoding below is Rd = r2, Rn = r1, Rm = r0. */
        /* SADD8: -128+127, -1+1, 1+2, 127+1 -- the last wraps to 0x80 but its
         * full-precision sum is still non-negative, so GE3 is set. */
        { 0xe6112f90u, 0x7f01ff80u, 0x0102017fu, 0x800300ffu, 0xeu, true,
          "SADD8 lanes and GE" },
        /* The exact encoding run32 stopped on: SADD8 lr, r1, lr. */
        { 0xe611ef9eu, 0x00000001u, 0u, 0u, 0u, false, "SADD8 lr,r1,lr decodes" },
        /* SADD16: -1+1 in the low lane must not carry into the high lane. */
        { 0xe6112f10u, 0x0001ffffu, 0x00010001u, 0x00020000u, 0xfu, true,
          "SADD16 no carry across lanes" },
        /* SSUB16 with a negative low lane clears the low GE pair. */
        { 0xe6112f70u, 0x00000000u, 0x00000001u, 0x0000ffffu, 0xcu, true,
          "SSUB16 GE from lane sign" },
        /* UADD8 sets GE from the unsigned carry out of each byte. */
        { 0xe6512f90u, 0x80ff0100u, 0x8001ff00u, 0x00000000u, 0xeu, true,
          "UADD8 GE from carry" },
        /* USUB8: GE means "no borrow", i.e. a >= b per byte. */
        { 0xe6512ff0u, 0x02000200u, 0x01010100u, 0x01ff0100u, 0xbu, true,
          "USUB8 GE from no-borrow" },
        /* UQADD8 saturates each byte at 0xff rather than wrapping. */
        { 0xe6612f90u, 0x80ff0100u, 0x8001ff00u, 0xffffff00u, 0u, false,
          "UQADD8 saturates" },
        /* QADD16 saturates a signed halfword at 0x7fff. */
        { 0xe6212f10u, 0x7fff0000u, 0x00010000u, 0x7fff0000u, 0u, false,
          "QADD16 saturates" },
        /* SHADD8 halves each lane: (4+2)>>1 == 3. */
        { 0xe6312f90u, 0x00000004u, 0x00000002u, 0x00000003u, 0u, false,
          "SHADD8 halves" },
        /* UHADD16 halves without sign extension: (0xffff+1)>>1 == 0x8000. */
        { 0xe6712f10u, 0x0000ffffu, 0x00000001u, 0x00008000u, 0u, false,
          "UHADD16 halves unsigned" },
        /* SASX: low = a.lo - b.hi = 3-2 = 1; high = a.hi + b.lo = 5+1 = 6. */
        { 0xe6112f30u, 0x00050003u, 0x00020001u, 0x00060001u, 0xfu, true,
          "SASX exchange" },
        /* SSAX: low = a.lo + b.hi = 3+2 = 5; high = a.hi - b.lo = 5-1 = 4. */
        { 0xe6112f50u, 0x00050003u, 0x00020001u, 0x00040005u, 0xfu, true,
          "SSAX exchange" },
    };

    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        uint32_t prog[] = { CASES[i].insn };
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, prog[0]);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        c.r[1]  = CASES[i].a;      /* Rn */
        c.r[0]  = CASES[i].b;      /* Rm */
        c.r[14] = CASES[i].b;
        CHECK(arm_step(&c) == ARM_OK, "%s: refused", CASES[i].what);
        if (CASES[i].check_ge) {
            CHECK(c.r[2] == CASES[i].expect, "%s: got %08x expected %08x",
                  CASES[i].what, c.r[2], CASES[i].expect);
            CHECK(((c.cpsr >> 16) & 0xfu) == CASES[i].ge,
                  "%s: GE %x expected %x", CASES[i].what,
                  (c.cpsr >> 16) & 0xfu, CASES[i].ge);
        }
    }

    /* The saturating and halving classes must leave GE alone. */
    {
        uint32_t prog[] = { 0xe6612f90u };            /* UQADD8 r2, r1, r0 */
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, prog[0]);
        arm_reset(&c, &g_bus);
        c.cpsr = ((c.cpsr & ~0x1fu) | ARM_MODE_SYS) | (0xfu << 16);
        CHECK(arm_step(&c) == ARM_OK, "UQADD8 refused");
        CHECK(((c.cpsr >> 16) & 0xfu) == 0xfu,
              "a saturating class must not write GE");
    }

    /* PC operands are UNPREDICTABLE and must refuse rather than branch. */
    {
        uint32_t prog[] = { 0xe611ff90u };            /* SADD8 pc, r1, r0 */
        CHECK(run_status(&c, prog, 1, 1) == ARM_UNDEFINED,
              "SADD8 with Rd == PC must refuse");
    }
}

/*
 * PKHBT / PKHTB / SEL -- the media instructions r205 stopped on.
 *
 * r205 halted with UNDEFINED INSTRUCTION on 0xe6844853, `PKHTB r4, r4, r3,
 * ASR #16`, at 0x33922c78 in pid 38, about 340 M instructions after a tap that
 * opened Notes. Every earlier boot missed it because nothing had run
 * halfword-packing code; the decoder's catch-all named PKH in a comment and
 * returned UNDEFINED.
 *
 * SEL is tested here rather than after the next stop because the parallel
 * add/sub family above already WRITES the GE flags and SEL is what reads them:
 * producer present, consumer trapping, is a one-instruction hole in the middle
 * of otherwise working code.
 */
static void test_pack_halfword_and_select(void) {
    arm_cpu_t c;
    static const struct {
        uint32_t insn, rn, rm, ge, expect;
        const char *what;
    } CASES[] = {
        /* PKHTB r2, r1, r0, ASR #16: top half from Rn, low half from Rm >> 16.
         * Rm is negative, so the arithmetic shift fills with ones and the low
         * half is its top half. */
        { 0xe6812850u, 0xaaaa1111u, 0xbbbb2222u, 0u, 0xaaaabbbbu,
          "PKHTB takes Rn top and shifted Rm bottom" },
        /* PKHBT r2, r1, r0, LSL #8: low half from Rn, top half from Rm << 8. */
        { 0xe6812410u, 0xaaaa1111u, 0x0000bb22u, 0u, 0x00bb1111u,
          "PKHBT takes Rn bottom and shifted Rm top" },
        /* PKHTB with imm5 == 0 encodes ASR #32, not "no shift": every bit of
         * the result becomes Rm's sign, so the low half is all ones here. */
        { 0xe6812050u, 0xaaaa1111u, 0x80000000u, 0u, 0xaaaaffffu,
          "PKHTB imm5 zero is ASR #32" },
        /* ...and the same encoding with a non-negative Rm gives all zeroes,
         * which distinguishes ASR #32 from a shift that was skipped. */
        { 0xe6812050u, 0xaaaa1111u, 0x7fffffffu, 0u, 0xaaaa0000u,
          "PKHTB ASR #32 of a positive Rm is zero" },
        /* SEL r2, r1, r0: each GE bit picks that byte lane from Rn, else Rm. */
        { 0xe6812fb0u, 0xaabbccddu, 0x11223344u, 0xau, 0xaa22cc44u,
          "SEL picks lanes by GE" },
        { 0xe6812fb0u, 0xaabbccddu, 0x11223344u, 0xfu, 0xaabbccddu,
          "SEL all GE set takes Rn entirely" },
        { 0xe6812fb0u, 0xaabbccddu, 0x11223344u, 0x0u, 0x11223344u,
          "SEL no GE set takes Rm entirely" },
    };

    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        uint32_t prog[] = { CASES[i].insn };
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, prog[0]);
        arm_reset(&c, &g_bus);
        c.cpsr = ((c.cpsr & ~0x1fu) | ARM_MODE_SYS) | (CASES[i].ge << 16);
        c.r[1] = CASES[i].rn;
        c.r[0] = CASES[i].rm;
        CHECK(arm_step(&c) == ARM_OK, "%s: refused", CASES[i].what);
        CHECK(c.r[2] == CASES[i].expect, "%s: got %08x expected %08x",
              CASES[i].what, c.r[2], CASES[i].expect);
    }

    /* The exact encoding that stopped r205, with its own registers. */
    {
        uint32_t prog[] = { 0xe6844853u };      /* PKHTB r4, r4, r3, ASR #16 */
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, prog[0]);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        c.r[4] = 0x638bff00u;                   /* r4 as r205 recorded it */
        c.r[3] = 0x556289ffu;                   /* r3 as r205 recorded it */
        CHECK(arm_step(&c) == ARM_OK, "the r205 encoding must not be UNDEFINED");
        CHECK(c.r[4] == 0x638b5562u, "r205 encoding: got %08x expected %08x",
              c.r[4], 0x638b5562u);
    }

    /* PC operands are UNPREDICTABLE and must refuse rather than branch. */
    {
        uint32_t prog[] = { 0xe681f850u };      /* PKHTB pc, r1, r0, ASR #16 */
        CHECK(run_status(&c, prog, 1, 1) == ARM_UNDEFINED,
              "PKHTB with Rd == PC must refuse");
    }
    {
        uint32_t prog[] = { 0xe681ffb0u };      /* SEL pc, r1, r0 */
        CHECK(run_status(&c, prog, 1, 1) == ARM_UNDEFINED,
              "SEL with Rd == PC must refuse");
    }
}

/*
 * The ARMv6 signed dual multiplies, and the multiply-accumulate media space
 * around them. r207 stopped on SMLAD (0xe7088e12) one billion instructions
 * further than r205's PKHTB, which is what argued for implementing the whole
 * family rather than discovering each member at forty minutes a run.
 */
static void test_signed_dual_multiply_family(void) {
    arm_cpu_t c;
    static const struct {
        uint32_t insn, rn, rm, ra, expect;
        bool     expect_q;
        const char *what;
    } CASES[] = {
        /* SMLAD r2,r1,r0,r3: 3*5 + 2*4 + 100 = 123. */
        { 0xe7023011u, 0x00020003u, 0x00040005u, 100u, 123u, false,
          "SMLAD adds both products and the accumulator" },
        /* SMUAD is the Ra == 15 encoding: same products, no accumulator. */
        { 0xe702f011u, 0x00020003u, 0x00040005u, 0u, 23u, false,
          "SMUAD has no accumulator" },
        /* SMLSD subtracts the high product from the low one. */
        { 0xe7023051u, 0x00020003u, 0x00040005u, 100u, 107u, false,
          "SMLSD subtracts the high product" },
        { 0xe702f051u, 0x00020003u, 0x00040005u, 0u, 7u, false,
          "SMUSD subtracts without an accumulator" },
        /* The X form swaps Rm's halves first: 3*4 + 2*5 + 100 = 122. */
        { 0xe7023031u, 0x00020003u, 0x00040005u, 100u, 122u, false,
          "SMLADX swaps the Rm halves" },
        /* Halves are SIGNED: 2*4 + (-1)*3 = 5, not 2*4 + 65535*3. */
        { 0xe702f011u, 0xffff0002u, 0x00030004u, 0u, 5u, false,
          "dual products sign-extend their halves" },
        /* Q is set when the full-precision result does not fit in 32 bits. */
        { 0xe7023011u, 0x7fff7fffu, 0x7fff7fffu, 0x7fffffffu, 0xfffe0001u, true,
          "SMLAD sets Q on overflow and keeps the low word" },
        /* SMMUL keeps the TOP word of a 32x32 product. */
        { 0xe752f011u, 0x40000000u, 0x40000000u, 0u, 0x10000000u, false,
          "SMMUL keeps the high word" },
        /* SMMLA adds Ra at bit 32 before taking the top word. */
        { 0xe7523011u, 0x40000000u, 0x40000000u, 1u, 0x10000001u, false,
          "SMMLA accumulates into the high word" },
        /* USAD8 sums |a-b| per byte: 3+1+1+3 = 8. */
        { 0xe782f011u, 0x01020304u, 0x04030201u, 0u, 8u, false,
          "USAD8 sums absolute byte differences" },
        /* USADA8 adds Ra to that sum. */
        { 0xe7823011u, 0x01020304u, 0x04030201u, 10u, 18u, false,
          "USADA8 adds the accumulator" },
    };

    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        uint32_t prog[] = { CASES[i].insn };
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, prog[0]);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        c.cpsr &= ~ARM_CPSR_Q;
        c.r[1] = CASES[i].rn;
        c.r[0] = CASES[i].rm;
        c.r[3] = CASES[i].ra;
        CHECK(arm_step(&c) == ARM_OK, "%s: refused", CASES[i].what);
        CHECK(c.r[2] == CASES[i].expect, "%s: got %08x expected %08x",
              CASES[i].what, c.r[2], CASES[i].expect);
        CHECK(((c.cpsr & ARM_CPSR_Q) != 0u) == CASES[i].expect_q,
              "%s: Q was %d", CASES[i].what,
              (c.cpsr & ARM_CPSR_Q) != 0u);
    }

    /* The exact encoding and register values r207 recorded. */
    {
        uint32_t prog[] = { 0xe7088e12u };      /* SMLAD r8, r2, lr, r8 */
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, prog[0]);
        arm_reset(&c, &g_bus);
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        c.r[2]  = 0x00020001u;
        c.r[14] = 0x10c23294u;
        c.r[8]  = 0x00002000u;
        CHECK(arm_step(&c) == ARM_OK, "the r207 encoding must not be UNDEFINED");
        CHECK(c.r[8] == 0x00007418u, "r207 encoding: got %08x expected %08x",
              c.r[8], 0x00007418u);
    }

    /* Q is sticky: a later non-overflowing operation must not clear it. */
    {
        uint32_t prog[] = { 0xe702f011u };      /* SMUAD r2, r1, r0 */
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, prog[0]);
        arm_reset(&c, &g_bus);
        c.cpsr = ((c.cpsr & ~0x1fu) | ARM_MODE_SYS) | ARM_CPSR_Q;
        c.r[1] = 0x00010001u; c.r[0] = 0x00010001u;
        CHECK(arm_step(&c) == ARM_OK, "SMUAD refused");
        CHECK((c.cpsr & ARM_CPSR_Q) != 0u, "Q must be sticky");
    }

    /* PC operands are UNPREDICTABLE and must refuse rather than branch. */
    {
        uint32_t prog[] = { 0xe70f3011u };      /* SMLAD pc, r1, r0, r3 */
        CHECK(run_status(&c, prog, 1, 1) == ARM_UNDEFINED,
              "SMLAD with Rd == PC must refuse");
    }
}

static void test_integer_divide_is_armv7_only(void) {
    /* SDIV/UDIV do not exist on the ARM1176. The first half of this test is the
     * important half: it pins the CURRENT target's behaviour, so adding a second
     * machine profile cannot quietly start executing an instruction that the
     * real hardware under iPhone OS 3 would have trapped. */
    arm_cpu_t c;
    static const struct {
        uint32_t insn, n, m, expect;
        const char *what;
    } CASES[] = {
        /* Rd = r2 (19:16), Rm = r0 (11:8), Rn = r1 (3:0). Bit 21 selects
         * unsigned, so SDIV is 0xe712f011 and UDIV is 0xe732f011. */
        { 0xe732f011u,  7u, 2u, 3u, "UDIV truncates toward zero" },
        { 0xe732f011u, 10u, 5u, 2u, "UDIV exact" },
        { 0xe732f011u,  1u, 0u, 0u, "UDIV by zero yields zero" },
        { 0xe732f011u, UINT32_C(0xffffffff), 2u, UINT32_C(0x7fffffff),
          "UDIV treats operands as unsigned" },
        { 0xe712f011u,  7u, 2u, 3u, "SDIV positive truncates toward zero" },
        /* C99 division truncates toward zero, which is what the architecture
         * specifies -- -7/2 is -3, not -4. */
        { 0xe712f011u, (uint32_t)-7, 2u, (uint32_t)-3,
          "SDIV negative truncates toward zero, not down" },
        { 0xe712f011u, (uint32_t)-7, (uint32_t)-2, 3u, "SDIV both negative" },
        { 0xe712f011u,  1u, 0u, 0u, "SDIV by zero yields zero" },
        /* INT_MIN / -1 overflows; the architecture defines the result as
         * INT_MIN rather than trapping, and C would call it undefined. */
        { 0xe712f011u, UINT32_C(0x80000000), UINT32_C(0xffffffff),
          UINT32_C(0x80000000), "SDIV INT_MIN/-1 wraps to INT_MIN" },
    };

    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        /* ARMv6: every one of these must still be refused. */
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, CASES[i].insn);
        arm_reset(&c, &g_bus);
        /* arch is core configuration, not runtime state, so arm_reset leaves it
         * alone -- a reset does not change which CPU you are. Set it here
         * rather than relying on the previous iteration. */
        c.arch = ARM_ARCH_V6_ARM1176;
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        c.r[1] = CASES[i].n;
        c.r[0] = CASES[i].m;
        CHECK(arm_step(&c) == ARM_UNDEFINED,
              "%s: must be UNDEFINED on the ARM1176", CASES[i].what);

        /* ARMv7: the same encoding executes and produces the defined result. */
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, CASES[i].insn);
        arm_reset(&c, &g_bus);
        c.arch = ARM_ARCH_V7_SWIFT;
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        c.r[1] = CASES[i].n;
        c.r[0] = CASES[i].m;
        CHECK(arm_step(&c) == ARM_OK, "%s: refused on ARMv7", CASES[i].what);
        CHECK(c.r[2] == CASES[i].expect, "%s: got %08x expected %08x",
              CASES[i].what, c.r[2], CASES[i].expect);
    }

    /* PC operands are UNPREDICTABLE in every position, even on ARMv7. */
    {
        static const struct { uint32_t insn; const char *what; } PCS[] = {
            { 0xe71ff011u, "Rd == PC" },
            { 0xe712f01fu, "Rn == PC" },
            { 0xe712ff11u, "Rm == PC" },
        };
        for (size_t i = 0; i < sizeof PCS / sizeof PCS[0]; i++) {
            memset(g_ram, 0, sizeof g_ram);
            m_w32(NULL, 0, PCS[i].insn);
            arm_reset(&c, &g_bus);
            c.arch = ARM_ARCH_V7_SWIFT;
            c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
            CHECK(arm_step(&c) == ARM_UNDEFINED,
                  "UDIV with %s must refuse", PCS[i].what);
        }
    }
}

static void test_movw_movt_are_armv7_only(void) {
    /* MOVW/MOVT are ARMv6T2; the ARM1176 is ARMv6K and does not have them.
     * ARMv7 compilers build every 32-bit constant this way, so they matter far
     * more than their size suggests. */
    arm_cpu_t c;

    /* MOVW r3, #0xbeef  -> imm4 = 0xb (19:16), imm12 = 0xeef, Rd = r3. */
    const uint32_t movw = 0xe30b3eefu;
    /* MOVT r3, #0xdead  -> imm4 = 0xd, imm12 = 0xead, Rd = r3. */
    const uint32_t movt = 0xe34d3eadu;

    /* ARMv6 must still refuse both. */
    for (int i = 0; i < 2; i++) {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, i ? movt : movw);
        arm_reset(&c, &g_bus);
        c.arch = ARM_ARCH_V6_ARM1176;
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        CHECK(arm_step(&c) == ARM_UNDEFINED,
              "%s must be UNDEFINED on the ARM1176", i ? "MOVT" : "MOVW");
    }

    /* ARMv7: MOVW zero-extends, then MOVT fills the top half and must leave the
     * bottom half alone -- the whole point of the pair. */
    {
        uint32_t prog[] = { movw, movt };
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, prog[0]);
        m_w32(NULL, 4, prog[1]);
        arm_reset(&c, &g_bus);
        c.arch = ARM_ARCH_V7_SWIFT;
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        c.r[3] = 0xffffffffu;          /* prove MOVW zero-extends, not merges */

        CHECK(arm_step(&c) == ARM_OK, "MOVW refused on ARMv7");
        CHECK(c.r[3] == 0x0000beefu, "MOVW gave %08x expected 0000beef", c.r[3]);
        CHECK(arm_step(&c) == ARM_OK, "MOVT refused on ARMv7");
        CHECK(c.r[3] == 0xdeadbeefu, "MOVT gave %08x expected deadbeef", c.r[3]);
    }

    /* Neither form writes flags, and Rd == PC is UNPREDICTABLE. */
    {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, movw);
        arm_reset(&c, &g_bus);
        c.arch = ARM_ARCH_V7_SWIFT;
        c.cpsr = ((c.cpsr & ~0x1fu) | ARM_MODE_SYS) | ARM_CPSR_Z | ARM_CPSR_C;
        CHECK(arm_step(&c) == ARM_OK, "MOVW refused");
        CHECK((c.cpsr & (ARM_CPSR_Z | ARM_CPSR_C)) == (ARM_CPSR_Z | ARM_CPSR_C),
              "MOVW must not write flags");
    }
    {
        memset(g_ram, 0, sizeof g_ram);
        m_w32(NULL, 0, 0xe30bfeefu);              /* MOVW pc, #0xbeef */
        arm_reset(&c, &g_bus);
        c.arch = ARM_ARCH_V7_SWIFT;
        c.cpsr = (c.cpsr & ~0x1fu) | ARM_MODE_SYS;
        CHECK(arm_step(&c) == ARM_UNDEFINED, "MOVW with Rd == PC must refuse");
    }
}

static void test_srs_and_rfe_stop_after_the_first_fault(void) {
    /* The first frame word is in an unmapped page while the second is a mapped,
     * watched device-like address. Once the first access faults, neither SRS nor
     * RFE may issue the second bus transaction. */
    uint32_t srs[] = { 0xf8cd0513u };             /* SRSIA sp,#SVC */
    arm_cpu_t c;
    split_setup(&c, srs, 1, 0u, 0x60000u, false);
    arm_set_mode(&c, ARM_MODE_IRQ);
    c.bank_r13[ARM_BANK_SVC] = 0x80000ffcu;
    c.r[14] = 0x11223344u;
    c.spsr[ARM_BANK_IRQ] = ARM_MODE_USR;
    c.r[15] = 0;
    g_watch_addr = 0x60000u; g_watch_reads32 = g_watch_writes32 = 0;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "SRS first-word fault must take a data abort");
    CHECK(g_watch_writes32 == 0u,
          "SRS issued %u second-word writes after its first word faulted",
          g_watch_writes32);

    uint32_t rfe[] = { 0xf8900a00u };             /* RFEIA r0 */
    arm_cpu_t d;
    split_setup(&d, rfe, 1, 0u, 0x60000u, false);
    arm_set_mode(&d, ARM_MODE_IRQ);
    d.r[0] = 0x80000ffcu;
    d.r[15] = 0;
    g_watch_addr = 0x60000u; g_watch_reads32 = g_watch_writes32 = 0;
    CHECK(arm_step(&d) == ARM_OK && d.r[15] == ARM_VEC_DATA_ABORT,
          "RFE first-word fault must take a data abort");
    CHECK(g_watch_reads32 == 0u,
          "RFE issued %u second-word reads after its first word faulted",
          g_watch_reads32);
    g_watch_addr = 0xffffffffu;
}

static void test_ldrd_strd_stop_after_the_first_faulting_word(void) {
    /* Same property for the doubleword pair, and the same trap: the second word
     * of an LDRD/STRD lies in the next page as often as not. VA 0x80000ffc is
     * in the absent page; the second word at 0x80001000 is mapped and watched,
     * so a stray access shows up as a bus read/write that must not exist. */
    uint32_t ldrd[] = { 0xe1c200d0u };            /* LDRD r0,r1,[r2] */
    arm_cpu_t c;
    split_setup(&c, ldrd, 1, 0u /* first page absent */, 0x60000u, false);
    c.r[0] = 0xdeadbeefu; c.r[1] = 0xfeedfaceu; c.r[2] = 0x80000ffcu;
    c.r[15] = 0;
    g_watch_addr = 0x60000u; g_watch_reads32 = g_watch_writes32 = 0;
    CHECK(arm_step(&c) == ARM_OK && c.r[15] == ARM_VEC_DATA_ABORT,
          "LDRD first-word fault must take a data abort");
    CHECK(g_watch_reads32 == 0u,
          "LDRD issued %u second-word reads after its first word faulted",
          g_watch_reads32);
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_PAGE_TRANSLATION &&
          (c.cp15.dfsr & (1u << 11)) == 0u && c.cp15.dfar == 0x80000ffcu,
          "LDRD fault state dfsr=%08x dfar=%08x", c.cp15.dfsr, c.cp15.dfar);
    CHECK(c.r[0] == 0xdeadbeefu && c.r[1] == 0xfeedfaceu &&
          c.r[2] == 0x80000ffcu,
          "faulting LDRD moved the pair or the base: %08x/%08x base=%08x",
          c.r[0], c.r[1], c.r[2]);

    /* The mirror: the SECOND word faults. DFAR names it, and the base-restored
     * abort model still forbids committing the half that did arrive. */
    arm_cpu_t d;
    split_setup(&d, ldrd, 1, 0x30000u, 0u /* second page absent */, false);
    m_w32(NULL, 0x30ffcu, 0x11223344u);
    d.r[0] = 0xdeadbeefu; d.r[1] = 0xfeedfaceu; d.r[2] = 0x80000ffcu;
    d.r[15] = 0;
    CHECK(arm_step(&d) == ARM_OK && d.r[15] == ARM_VEC_DATA_ABORT &&
          d.cp15.dfar == 0x80001000u,
          "LDRD second-word fault pc=%08x dfar=%08x", d.r[15], d.cp15.dfar);
    CHECK(d.r[0] == 0xdeadbeefu && d.r[1] == 0xfeedfaceu &&
          d.r[2] == 0x80000ffcu,
          "LDRD committed a half pair across a fault: %08x/%08x base=%08x",
          d.r[0], d.r[1], d.r[2]);

    uint32_t strd[] = { 0xe1c200f0u };            /* STRD r0,r1,[r2] */
    arm_cpu_t e;
    split_setup(&e, strd, 1, 0u, 0x60000u, false);
    e.r[0] = 0x11223344u; e.r[1] = 0x55667788u; e.r[2] = 0x80000ffcu;
    e.r[15] = 0;
    g_watch_addr = 0x60000u; g_watch_reads32 = g_watch_writes32 = 0;
    CHECK(arm_step(&e) == ARM_OK && e.r[15] == ARM_VEC_DATA_ABORT,
          "STRD first-word fault must take a data abort");
    CHECK(g_watch_writes32 == 0u,
          "STRD issued %u second-word writes after its first word faulted",
          g_watch_writes32);
    CHECK((e.cp15.dfsr & 0xfu) == ARM_FSR_PAGE_TRANSLATION &&
          (e.cp15.dfsr & (1u << 11)) != 0u && e.cp15.dfar == 0x80000ffcu,
          "STRD fault state dfsr=%08x dfar=%08x — a store must set WnR",
          e.cp15.dfsr, e.cp15.dfar);
    CHECK(e.r[2] == 0x80000ffcu, "faulting STRD moved its base to %08x", e.r[2]);
    g_watch_addr = 0xffffffffu;
}

static void test_unaligned_access_spanning_two_pages(void) {
    /* VA 0x80000ffe..0x80001001: two bytes in the frame at PA 0x30000 and two
     * in the frame at PA 0x60000. Translating the base once and doing a single
     * 32-bit bus read at 0x30ffe picks up 0x31000/0x31001 for the top half —
     * memory belonging to whatever else happens to sit after that frame, which
     * is why a decoy is planted there: it returned 0x8899bbaa before the fix. */
    uint32_t p[] = { 0xe59f1008 /*LDR r1,[pc,#8] -> literal at 0x10*/,
                     0xe5910000 /*LDR r0,[r1]                      */,
                     0xeafffffe /*B .                              */,
                     0x00000000,
                     0x80000ffeu };
    arm_cpu_t c; split_setup(&c, p, 5, 0x30000u, 0x60000u, false);
    m_w8(NULL, 0x30ffeu, 0xaa); m_w8(NULL, 0x30fffu, 0xbb);
    m_w8(NULL, 0x60000u, 0xcc); m_w8(NULL, 0x60001u, 0xdd);
    m_w8(NULL, 0x31000u, 0x99); m_w8(NULL, 0x31001u, 0x88);  /* the decoy */
    arm_step(&c); arm_step(&c);
    CHECK(c.r[0] == 0xddccbbaau,
          "r0=%08x expect ddccbbaa — the two halves come from PA 0x30ffe and "
          "PA 0x60000, not from one run of bytes at 0x30ffe", c.r[0]);

    /* The storing mirror: each half must land in its own frame, and nothing may
     * be written past the end of the first one. */
    uint32_t q[] = { 0xe59f1008, 0xe5810000 /*STR r0,[r1]*/, 0xeafffffe,
                     0x00000000, 0x80000ffeu };
    arm_cpu_t d; split_setup(&d, q, 5, 0x30000u, 0x60000u, false);
    arm_step(&d);
    d.r[0] = 0x11223344u;
    arm_step(&d);
    CHECK(m_r8(NULL, 0x30ffeu) == 0x44 && m_r8(NULL, 0x30fffu) == 0x33,
          "%02x %02x expect 44 33 in the first frame",
          m_r8(NULL, 0x30ffeu), m_r8(NULL, 0x30fffu));
    CHECK(m_r8(NULL, 0x60000u) == 0x22 && m_r8(NULL, 0x60001u) == 0x11,
          "%02x %02x expect 22 11 in the SECOND frame",
          m_r8(NULL, 0x60000u), m_r8(NULL, 0x60001u));
    CHECK(m_r8(NULL, 0x31000u) == 0 && m_r8(NULL, 0x31001u) == 0,
          "the store must not run past the end of the first physical frame");

    /* A halfword straddling the same boundary, and — the other side of the
     * test — a word ending exactly ON the boundary, which does NOT cross and
     * must still be served by a single whole-word bus access. */
    uint32_t h[] = { 0xe59f1008, 0xe1d100b0 /*LDRH r0,[r1]*/, 0xeafffffe,
                     0x00000000, 0x80000fffu };
    arm_cpu_t e; split_setup(&e, h, 5, 0x30000u, 0x60000u, false);
    m_w8(NULL, 0x30fffu, 0xbb); m_w8(NULL, 0x60000u, 0xcc);
    arm_step(&e); arm_step(&e);
    CHECK(e.r[0] == 0xccbbu, "r0=%08x expect ccbb for a straddling LDRH", e.r[0]);

    uint32_t w[] = { 0xe59f1008, 0xe5910000, 0xeafffffe, 0x00000000, 0x80000ffcu };
    arm_cpu_t g; split_setup(&g, w, 5, 0x30000u, 0x60000u, false);
    m_w32(NULL, 0x30ffcu, 0x12345678u);
    arm_step(&g); arm_step(&g);
    CHECK(g.r[0] == 0x12345678u,
          "r0=%08x expect 12345678 — 0xffc..0xfff is the last word wholly inside "
          "the page and must not be split", g.r[0]);
}

static void test_unaligned_access_faulting_on_the_second_page(void) {
    /* The half that faults is the one that must be reported. sleh_abort page-
     * aligns DFAR and repairs that page, so a fault on the second page reported
     * against the base address maps in the page the access already had and
     * re-executes the same instruction forever. */
    uint32_t p[] = { 0xe59f1008, 0xe5910000 /*LDR r0,[r1]*/, 0xeafffffe,
                     0x00000000, 0x80000ffeu };
    arm_cpu_t c; split_setup(&c, p, 5, 0x30000u, 0u /*second page absent*/, false);
    arm_step(&c);
    c.r[0] = 0xdeadbeefu;
    arm_step(&c);
    CHECK(c.r[15] == ARM_VEC_DATA_ABORT, "pc=%08x expect 10 (data abort)", c.r[15]);
    CHECK((c.cp15.dfsr & 0xfu) == ARM_FSR_PAGE_TRANSLATION,
          "dfsr=%08x expect a page translation fault", c.cp15.dfsr);
    CHECK((c.cp15.dfsr & (1u << 11)) == 0, "dfsr=%08x: a load must not set WnR",
          c.cp15.dfsr);
    CHECK(c.cp15.dfar == 0x80001000u,
          "dfar=%08x expect 80001000 — the first byte that lies in the SECOND "
          "page, not the base 0x80000ffe", c.cp15.dfar);
    CHECK(c.r[0] == 0xdeadbeefu,
          "r0=%08x: base-restored abort model — the destination must not move",
          c.r[0]);
    CHECK(c.r[1] == 0x80000ffeu, "r1=%08x expect the base unchanged", c.r[1]);

    /* A store that faults on its second page. DFAR and WnR as above — and the
     * bytes that landed in the first page STAY there. Hardware issues the two
     * halves as separate bus transactions and cannot retract one that has
     * completed; the architecture restores the base *register*, not memory.
     * Re-execution after the handler maps the page rewrites the same bytes. */
    uint32_t q[] = { 0xe59f1008, 0xe5810000 /*STR r0,[r1]*/, 0xeafffffe,
                     0x00000000, 0x80000ffeu };
    arm_cpu_t d; split_setup(&d, q, 5, 0x30000u, 0u, false);
    arm_step(&d);
    d.r[0] = 0x11223344u;
    arm_step(&d);
    CHECK(d.r[15] == ARM_VEC_DATA_ABORT, "pc=%08x expect 10 (data abort)", d.r[15]);
    CHECK(d.cp15.dfar == 0x80001000u,
          "dfar=%08x expect 80001000 for the storing form too", d.cp15.dfar);
    CHECK((d.cp15.dfsr & (1u << 11)) != 0,
          "dfsr=%08x: a store must set WnR", d.cp15.dfsr);
    CHECK(m_r8(NULL, 0x30ffeu) == 0x44 && m_r8(NULL, 0x30fffu) == 0x33,
          "%02x %02x expect 44 33: the first page's bytes are already committed "
          "when the second page aborts", m_r8(NULL, 0x30ffeu), m_r8(NULL, 0x30fffu));
    CHECK(d.r[1] == 0x80000ffeu, "r1=%08x expect the base unchanged", d.r[1]);

    /* And when it is the FIRST page that is missing, the base is the right
     * answer and nothing is written anywhere. */
    arm_cpu_t e; split_setup(&e, q, 5, 0u, 0x60000u, false);
    arm_step(&e);
    e.r[0] = 0x11223344u;
    arm_step(&e);
    CHECK(e.cp15.dfar == 0x80000ffeu,
          "dfar=%08x expect 80000ffe when the first page is the bad one",
          e.cp15.dfar);
    CHECK(m_r8(NULL, 0x60000u) == 0 && m_r8(NULL, 0x60001u) == 0,
          "a fault on the first half must not let the second half through");
}

/*
 * XN — execute never.
 *
 * Confirmed live in xnu-1357.5.30 before being implemented: _nx_enabled
 * (0xc020e1b8) is a __DATA global initialised to 1 that nothing in the image
 * writes, and Thumb _pmap_enter (0xc006056c) builds its small-page template as
 * "pa | 2" but reaches an ORR that makes it "pa | 3" — XN set — whenever the
 * mapping is not VM_PROT_EXECUTE and both _nx_enabled and the per-pmap flag at
 * pmap+0x420 are non-zero. _pmap_disable_NX (0xc005fe44) is five instructions
 * that store 0 to that flag, and it is the only opt-out.
 */
static void test_xn_blocks_fetch_from_a_small_page(void) {
    uint32_t p[] = { 0xe59f0008 /*LDR r0,[pc,#8] -> literal at 0x10*/,
                     0xe1a0f000 /*MOV pc,r0                        */,
                     0xeafffffe /*B .                              */,
                     0x00000000,
                     0x80001000u };

    /* The permitted case first, so the fault below is known to be the XN bit
     * and not the mapping being broken: same page, XN clear, executes. */
    arm_cpu_t c; split_setup(&c, p, 5, 0x30000u, 0x60000u, false);
    m_w32(NULL, 0x60000u, 0xe3a02007u);             /* MOV r2,#7 at the target */
    arm_step(&c); arm_step(&c); arm_step(&c);
    CHECK(c.r[2] == 7, "r2=%u expect 7 — a page without XN must execute", c.r[2]);

    /* The same mapping with XN set. */
    arm_cpu_t d; split_setup(&d, p, 5, 0x30000u, 0x60000u, true);
    m_w32(NULL, 0x60000u, 0xe3a02007u);
    d.cp15.dfsr = 0xdeadbeefu; d.cp15.dfar = 0xcafebabeu;
    arm_step(&d);
    d.r[2] = 0x5a5a5a5au;
    arm_step(&d);
    CHECK(d.r[15] == 0x80001000u, "pc=%08x expect 80001000 before the fetch", d.r[15]);
    arm_step(&d);
    CHECK(d.r[15] == ARM_VEC_PREFETCH, "pc=%08x expect 0c (prefetch abort)", d.r[15]);
    CHECK((d.cp15.ifsr & 0xfu) == ARM_FSR_PAGE_PERMISSION,
          "ifsr=%08x expect a page permission fault — XN is reported as a "
          "permission fault", d.cp15.ifsr);
    CHECK((d.cp15.ifsr & (1u << 11)) == 0,
          "ifsr=%08x: IFSR has no WnR field", d.cp15.ifsr);
    CHECK(d.cp15.ifar == 0x80001000u, "ifar=%08x expect 80001000", d.cp15.ifar);
    CHECK(d.cp15.dfsr == 0xdeadbeefu && d.cp15.dfar == 0xcafebabeu,
          "an XN fault is a prefetch abort and must not touch the data side");
    CHECK(d.r[2] == 0x5a5a5a5au,
          "r2=%08x: the execute-never page must not have executed", d.r[2]);

    /* XN is a fetch-only attribute: the same page stays readable and writable.
     * If it did not, marking the heap non-executable would make it unusable. */
    uint32_t pa = 0;
    CHECK(arm_mmu_translate(&d, 0x80001000u, ARM_ACCESS_READ, true, &pa) == 0
          && pa == 0x60000u,
          "pa=%08x: an XN page must still translate for a read", pa);
    CHECK(arm_mmu_translate(&d, 0x80001000u, ARM_ACCESS_WRITE, true, &pa) == 0,
          "an XN page must still translate for a write");
    CHECK((arm_mmu_translate(&d, 0x80001000u, ARM_ACCESS_FETCH, true, &pa) & 0xfu)
          == ARM_FSR_PAGE_PERMISSION,
          "only ARM_ACCESS_FETCH may see the XN bit");

    /* A manager domain cannot generate a permission fault at all, and an XN
     * violation is a permission fault, so it is not checked there either. */
    d.cp15.dacr = 3u;
    CHECK(arm_mmu_translate(&d, 0x80001000u, ARM_ACCESS_FETCH, true, &pa) == 0,
          "a manager domain must not raise an XN fault");
}

static void test_xn_on_a_section_and_the_xp_gate(void) {
    /* Sections carry XN in bit 4. The section planted at index 0x800 maps
     * 0x80000000 onto physical 0, so 0x80000008 is the MOV r2,#7 below. */
    uint32_t p[] = { 0xe59f0008 /*LDR r0,[pc,#8]*/, 0xe1a0f000 /*MOV pc,r0*/,
                     0xe3a02007 /*MOV r2,#7     */, 0xeafffffe /*B .      */,
                     0x80000008u };
    struct { bool xn, xp; } cases[] = { {false, true}, {true, true}, {true, false} };
    for (unsigned k = 0; k < 3; k++) {
        arm_cpu_t c;
        memset(g_ram, 0, sizeof g_ram);
        for (unsigned i = 0; i < 5; i++) m_w32(NULL, i * 4, p[i]);
        m_w32(NULL, 0x4000 + (0x000u << 2), (3u << 10) | 2u);
        m_w32(NULL, 0x4000 + (0x800u << 2),
              (3u << 10) | 2u | (cases[k].xn ? (1u << 4) : 0u));
        arm_reset(&c, &g_bus);
        c.cp15.ttbr0  = 0x4000;
        c.cp15.dacr   = 1u;
        c.cp15.sctlr |= ARM_SCTLR_M | (cases[k].xp ? ARM_SCTLR_XP : 0u);
        c.cpsr = (c.cpsr & ~ARM_CPSR_MODE_MASK) | ARM_MODE_SYS;
        c.r[15] = 0;
        c.r[2] = 0x5a5a5a5au;
        arm_step(&c); arm_step(&c); arm_step(&c);

        if (cases[k].xn && cases[k].xp) {
            CHECK(c.r[15] == ARM_VEC_PREFETCH,
                  "pc=%08x expect 0c: an XN section must abort on fetch", c.r[15]);
            CHECK((c.cp15.ifsr & 0xfu) == ARM_FSR_SECTION_PERMISSION,
                  "ifsr=%08x expect a SECTION permission fault", c.cp15.ifsr);
            CHECK(c.cp15.ifar == 0x80000008u, "ifar=%08x expect 80000008",
                  c.cp15.ifar);
        } else {
            /* xn && !xp is the inert case: with SCTLR.XP clear the descriptors
             * are the ARMv5-compatible layout, where bit 4 is not XN at all, so
             * reading it as XN would invent a fault the hardware never takes. */
            CHECK(c.r[2] == 7,
                  "r2=%08x expect 7 (xn=%d xp=%d): the fetch must be permitted",
                  c.r[2], (int)cases[k].xn, (int)cases[k].xp);
        }
    }
}

static void test_direct_write_cache_requires_explicit_consent(void) {
    static const uint32_t prog[] = {
        0xe5801000u, /* STR r1,[r0] */
        0xe5801000u,
        0xe5801000u,
        0xe5801000u,
        0xe5801000u,
    };
    arm_bus_t bus = g_bus;
    arm_cpu_t c;
    uint32_t value = 0u;

    memset(g_ram, 0, sizeof g_ram);
    for (unsigned i = 0u; i < sizeof prog / sizeof prog[0]; i++)
        m_w32(NULL, i * 4u, prog[i]);
    bus.host_ram_write = m_host_ram_write;
    arm_reset(&c, &bus);
    c.cpsr = (c.cpsr & ~UINT32_C(0x1f)) | ARM_MODE_SYS;
    c.r[0] = UINT32_C(0x8000);
    c.r[1] = UINT32_C(0x11223344);
    g_watch_addr = c.r[0];
    g_watch_writes32 = 0u;

    CHECK(arm_step(&c) == ARM_OK && arm_step(&c) == ARM_OK,
          "consented direct stores execute");
    memcpy(&value, &g_ram[c.r[0]], sizeof value);
    CHECK(value == c.r[1], "direct-store value=%08x", value);
    CHECK(g_watch_writes32 == 1u,
          "first store fills through bus, second bypasses it: writes=%u",
          g_watch_writes32);
    CHECK(c.dwrite_misses == 1u && c.dwrite_hits == 1u,
          "direct-write counters miss/hit=%llu/%llu",
          (unsigned long long)c.dwrite_misses,
          (unsigned long long)c.dwrite_hits);

    /* Revoking the callback must defeat an already-filled pointer immediately,
     * without depending on a TLB flush or cache clear. */
    bus.host_ram_write = NULL;
    c.r[1] = UINT32_C(0x55667788);
    CHECK(arm_step(&c) == ARM_OK && g_watch_writes32 == 2u,
          "revoked consent returned to the bus, writes=%u",
          g_watch_writes32);

    /* Re-enable and change the translation generation. The old host pointer is
     * still present but must miss on generation before the following hit. */
    bus.host_ram_write = m_host_ram_write;
    arm_mmu_tlb_flush(&c);
    c.r[1] = UINT32_C(0xaabbccdd);
    CHECK(arm_step(&c) == ARM_OK && arm_step(&c) == ARM_OK,
          "generation-refilled direct stores execute");
    CHECK(g_watch_writes32 == 3u,
          "generation miss used the bus exactly once, writes=%u",
          g_watch_writes32);
    CHECK(c.dwrite_misses == 2u && c.dwrite_hits == 2u,
          "post-flush miss/hit=%llu/%llu",
          (unsigned long long)c.dwrite_misses,
          (unsigned long long)c.dwrite_hits);

    arm_reset(&c, &bus);
    bool empty = true;
    for (unsigned i = 0u; i < ARM_DREAD_ENTRIES; i++)
        if (c.dwrite[i].host) empty = false;
    CHECK(empty, "reset clears every process-local DWRITE pointer");
    g_watch_addr = UINT32_MAX;
}

int main(void) {
    printf("S5LBox ARMv6 interpreter tests\n");
    test_mov_imm();
    test_add_reg();
    test_sub_flags();
    test_subs_negative();
    test_adds_overflow();
    test_nzcv_updates_preserve_the_rest_of_cpsr();
    test_barrel_lsl();
    test_register_shifted_pc_operands_are_unpredictable();
    test_branch();
    test_bl_sets_lr();
    test_ldr_str();
    test_direct_write_cache_requires_explicit_consent();
    test_str_and_stm_store_pc_plus_12();
    test_ldrb();
    test_cond_not_taken();
    test_mul();
    test_dsp_smul_halfword_selectors_and_real_alias();
    test_dsp_smla_wraps_and_sets_sticky_q();
    test_dsp_smlal_sign_carry_wrap_and_alias();
    test_dsp_word_halfword_extract_and_accumulate();
    test_dsp_multiply_unpredictable_and_reserved_forms_trap();
    test_orr_bic_mvn();
    test_bx_branches();
    test_exception_return_to_thumb_keeps_halfword();
    test_exception_return_to_arm_stays_word_aligned();
    test_ldm_exception_return_takes_state_from_spsr();
    test_rfe_aligns_for_the_restored_state();
    test_srs_and_rfe_reject_unprivileged_execution();
    test_exception_returns_reject_invalid_modes_before_mutation();
    test_setend_le_runs_be_traps();
    test_umull_and_smull();
    test_umlal_accumulates();
    test_clz();
    test_qadd_saturates_and_sets_q();
    test_swp_exchanges();
    test_swp_legacy_unaligned_word_semantics();
    test_ldrexd_strexd_roundtrip();
    test_clrex_makes_strex_fail();
    test_failed_strex_still_checks_write_permission();
    test_exception_clears_exclusive_monitor();
    test_imm_dp_not_trapped();
    test_strh_ldrh();
    test_ldrsb_sign_extends();
    test_ldrsh_sign_extends();
    test_stmia_ldmia();
    test_push_pop();
    test_ldm_to_pc_branches();
    test_plain_loads_to_pc_reject_arm_halfword_targets();
    test_unaligned_ldr_to_pc_never_reads_memory();
    test_banked_sp_per_mode();
    test_fiq_banks_r8_r12();
    test_mrs_reads_cpsr();
    test_msr_switches_mode();
    test_swi_enters_svc();
    test_exception_return_restores_mode();
    test_cp15_reads_midr();
    test_cp15_cpuid_feature_bank();
    test_cp15_cpuid_scheme_grades_as_armv6();
    test_cp15_id_dfr0_matches_absent_debug_unit();
    test_cp15_sctlr_roundtrip();
    test_cp15_c1_is_gated_on_crm_zero();
    test_cp15_ttbr0_roundtrip();
    test_cp15_ttbcr_masks_only_reserved_bits();
    test_cp15_cache_op_is_accepted();
    test_cp15_wfi_uses_only_the_exact_privileged_hook();
    test_high_vectors();
    test_mmu_disabled_is_identity();
    test_mmu_section_translation();
    test_mmu_unmapped_faults();
    test_mmu_user_write_permission();
    test_mmu_small_page_translation();
    test_data_abort_taken();
    test_pld_is_a_nop_not_a_branch();
    test_unconditional_space_traps();
    test_clz_does_not_corrupt_cpsr();
    test_msr_still_works();
    test_arm_media_extend_and_reverse();
    test_arm_media_pair_extend();
    test_arm_media_reverse_edges();
    test_arm_media_saturate();
    test_apx_makes_mapping_read_only();
    test_xp0_ignores_extended_apx_bits();
    test_xp0_large_and_small_ap_subpages();
    test_sctlr_sr_legacy_permissions();
    test_arm1176_rejects_fine_page_tables();
    test_page_translation_fault_precedes_page_domain_fault();
    test_force_access_flag_faults_precede_domain_permissions();
    test_fetch_cache_refill_requires_an_exact_live_witness();
    test_abort_restores_base_register();
    test_sctlr_a_faults_ordinary_unaligned_accesses();
    test_sctlr_u_selects_legacy_or_armv6_unaligned_data();
    test_multiword_alignment_depends_on_sctlr_u_a();
    test_arm_multiword_base_writeback_restrictions();
    test_single_transfer_zero_post_same_base_load();
    test_single_transfer_unpredictable_forms_trap_before_bus();
    test_ldrd_strd_transfer_the_register_pair();
    test_ldrd_strd_alignment_is_the_armv6_word_rule();
    test_ldrd_strd_operand_restrictions_trap_before_bus();
    test_exclusive_alignment_is_never_silently_fixed();
    test_atomic_operand_aliases_are_rejected_before_bus();
    test_thumb_mov_add();
    test_thumb_lsl_flags();
    test_thumb_push_pop();
    test_thumb_multiword_alignment_uses_strict_rules();
    test_thumb_conditional_branch();
    test_arm_to_thumb_and_back();
    test_bx_blx_register_reject_invalid_targets_without_side_effects();
    test_thumb_bl_pair();
    test_thumb_extend_and_reverse();
    test_thumb_rev();
    test_thumb_cps();
    test_mmu_supersection();
    test_thumb_blx_suffix_is_not_a_branch();
    test_user_bank_stm();
    test_mmu_ttbcr_n0_ignores_ttbr1();
    test_mmu_ttbcr_n2_low_va_uses_shrunk_ttbr0();
    test_mmu_ttbcr_n2_kernel_va_uses_ttbr1();
    test_mmu_ttbcr_n2_split_boundary();
    test_mmu_kernel_mapping_survives_ttbr0_pmap_switch();
    test_mmu_ttbcr_n1_table_geometry();
    test_mmu_ttbcr_n3_table_geometry();
    test_mmu_ttbcr_pd_bits_suppress_selected_walk();
    test_dfsr_wnr_write_vs_read();
    test_data_abort_write_sets_dfsr_wnr();
    test_prefetch_abort_never_sets_wnr();
    test_dfar_is_the_faulting_word_of_a_block_transfer();
    test_cps_is_a_nop_in_user_mode();
    test_vfp_disabled_vectors_the_guest();
    test_vfp_enabled_still_halts();
    test_non_vfp_undefined_still_halts();
    test_vfp_undef_entry_state();
    test_vfp_sysreg_access_follows_fpexc();
    test_vfp_cpacr_denial_vectors();
    test_vfp_lazy_trap_cannot_loop();
    test_privileged_svc_hook_handles_a32();
    test_privileged_svc_hook_redirects_a32_to_thumb();
    test_privileged_svc_hook_nonhandled_is_transactional();
    test_privileged_svc_hook_a32_error_halts_transactionally();
    test_privileged_svc_hook_obeys_a32_guards();
    test_privileged_svc_hook_handles_thumb();
    test_privileged_svc_hook_redirects_thumb_to_a32();
    test_privileged_svc_hook_thumb_error_and_user_guard();
    test_swi_entry_state_from_user_mode();
    test_thumb_swi_lr_is_the_next_halfword();
    test_irq_sets_a_but_swi_does_not();
    test_exception_entry_takes_cpsr_e_from_sctlr_ee();
    test_xnu_syscall_frame_round_trip();
    test_syscall_error_flag_reaches_user_through_the_spsr();
    test_user_bank_ldm_writes_the_user_bank();
    test_user_bank_stm_from_fiq_uses_the_user_r8_r12();
    test_user_bank_transfer_with_writeback_traps();
    test_user_bank_transfers_reject_user_and_system_modes();
    test_srs_stores_lr_and_spsr_of_the_current_mode();
    test_subs_pc_lr_4_returns_from_fiq();
    test_ldrt_from_svc_translates_as_unprivileged();
    test_strt_from_svc_translates_as_unprivileged();
    test_user_mode_load_uses_user_permissions();
    test_cp15_is_privileged_from_user_mode();
    test_cp15_thread_id_registers_stay_user_accessible();
    test_unaligned_access_spanning_two_pages();
    test_unaligned_access_faulting_on_the_second_page();
    test_parallel_add_sub_family();
    test_pack_halfword_and_select();
    test_signed_dual_multiply_family();
    test_integer_divide_is_armv7_only();
    test_movw_movt_are_armv7_only();
    test_srs_and_rfe_stop_after_the_first_fault();
    test_ldrd_strd_stop_after_the_first_faulting_word();
    test_xn_blocks_fetch_from_a_small_page();
    test_xn_on_a_section_and_the_xp_gate();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
