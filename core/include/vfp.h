/*
 * S5LBox — VFPv2 (VFP11) coprocessor, public interface.
 *
 * The ARM1176JZF-S carries a VFP11 unit implementing VFPv2: 32 single-precision
 * registers s0-s31 aliased onto 16 double-precision registers d0-d15. There is
 * no d16-d31 and there is no Advanced SIMD/NEON on this part.
 *
 * Everything about WHY this exists, and every floating-point semantic this
 * implementation does and does not model, is documented at the top of
 * core/src/arm/vfp.c. Read that before using anything here.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_VFP_H
#define S5LBOX_VFP_H

#include "arm.h"

/* ---------------------------------------------------------------- FPSCR ---
 * The VFP status and control register. Bit positions are architectural
 * (ARM DDI 0100I, C1.2 / ARM DDI 0301H, 20.1.2).
 */
#define ARM_FPSCR_N      (1u << 31)  /* comparison: less-than or unordered   */
#define ARM_FPSCR_Z      (1u << 30)  /* comparison: equal                    */
#define ARM_FPSCR_C      (1u << 29)  /* comparison: >=, or unordered         */
#define ARM_FPSCR_V      (1u << 28)  /* comparison: unordered                */
#define ARM_FPSCR_NZCV   0xf0000000u
#define ARM_FPSCR_DN     (1u << 25)  /* default NaN mode                     */
#define ARM_FPSCR_FZ     (1u << 24)  /* flush-to-zero mode                   */
#define ARM_FPSCR_RMODE  (3u << 22)  /* 00 RN, 01 RP, 10 RM, 11 RZ           */
#define ARM_FPSCR_STRIDE (3u << 20)  /* short-vector stride                  */
#define ARM_FPSCR_LEN    (7u << 16)  /* short-vector length minus one        */
/* Trap enables. Setting one asks the VFP to *bounce* the instruction to
 * support code rather than set the matching cumulative bit. */
#define ARM_FPSCR_IDE    (1u << 15)
#define ARM_FPSCR_IXE    (1u << 12)
#define ARM_FPSCR_UFE    (1u << 11)
#define ARM_FPSCR_OFE    (1u << 10)
#define ARM_FPSCR_DZE    (1u <<  9)
#define ARM_FPSCR_IOE    (1u <<  8)
#define ARM_FPSCR_ENABLES (ARM_FPSCR_IDE | ARM_FPSCR_IXE | ARM_FPSCR_UFE | \
                           ARM_FPSCR_OFE | ARM_FPSCR_DZE | ARM_FPSCR_IOE)
/* Cumulative (sticky) exception flags. Set by hardware, cleared by software. */
#define ARM_FPSCR_IDC    (1u <<  7)  /* input denormal                       */
#define ARM_FPSCR_IXC    (1u <<  4)  /* inexact                              */
#define ARM_FPSCR_UFC    (1u <<  3)  /* underflow                            */
#define ARM_FPSCR_OFC    (1u <<  2)  /* overflow                             */
#define ARM_FPSCR_DZC    (1u <<  1)  /* division by zero                     */
#define ARM_FPSCR_IOC    (1u <<  0)  /* invalid operation                    */

/*
 * The bits VFPv2 defines. Everything else is reserved and reads as zero on the
 * ARM1176, so writes to those bits are dropped rather than stored — a guest
 * that writes 0xffffffff and reads it back must see hardware's answer.
 */
#define ARM_FPSCR_WMASK  0xf3f79f9fu

/* ------------------------------------------------------- register file --- */
/*
 * s0-s31 as raw bit patterns, in register-number order. dN occupies the pair
 * (s[2N], s[2N+1]) with s[2N] holding the LOW-order word — the little-endian
 * layout the ARM1176 uses, and the reason a VSTM of {d0} and a VSTM of
 * {s0,s1} write the same four words in the same order.
 */
static inline uint32_t vfp_get_s(const arm_cpu_t *c, unsigned n) {
    return c->vfp_s[n & 31u];
}
static inline void vfp_set_s(arm_cpu_t *c, unsigned n, uint32_t v) {
    c->vfp_s[n & 31u] = v;
}
static inline uint64_t vfp_get_d(const arm_cpu_t *c, unsigned n) {
    n = (n & 15u) * 2u;
    return (uint64_t)c->vfp_s[n] | ((uint64_t)c->vfp_s[n + 1u] << 32);
}
static inline void vfp_set_d(arm_cpu_t *c, unsigned n, uint64_t v) {
    n = (n & 15u) * 2u;
    c->vfp_s[n]      = (uint32_t)v;
    c->vfp_s[n + 1u] = (uint32_t)(v >> 32);
}

/* ------------------------------------------------------------ the unit --- */

/*
 * Data-side memory, supplied by the interpreter. These are the interpreter's
 * own translating accessors: they walk the MMU with the current privilege and
 * latch a fault on the CPU (cpu->abort_pending), which arm_step turns into a
 * data abort once the instruction finishes. VFP load/store must go through
 * them and must check abort_pending between words for exactly that reason.
 */
typedef struct vfp_bus {
    uint32_t (*read32 )(arm_cpu_t *c, uint32_t va);
    void     (*write32)(arm_cpu_t *c, uint32_t va, uint32_t v);
} vfp_bus_t;

/*
 * True when CP15 CPACR grants the current mode access to CP10 and CP11.
 * Lives here rather than in the interpreter because it is half of the
 * lazy-enable gate that vfp_execute also applies.
 */
bool vfp_cpacr_permits(const arm_cpu_t *c);

/* True when FPEXC.EN is set, i.e. VFP is enabled for the running thread. */
bool vfp_enabled(const arm_cpu_t *c);

/*
 * Execute one VFP encoding. `insn` must already have been identified as a
 * cp10/cp11 encoding by the caller and its condition code must already have
 * passed. Returns ARM_OK; ARM_UNDEFINED for anything not implemented or for a
 * disabled-unit access (the lazy-enable trap); or ARM_GUEST_UNDEFINED for an
 * architecturally denied access. arm_step recognizes the disabled-unit
 * ARM_UNDEFINED separately and routes it plus ARM_GUEST_UNDEFINED to the
 * guest's handler while keeping capability gaps fail-closed.
 *
 * VFP never writes r15, so the caller's `next` is unaffected.
 */
arm_status_t vfp_execute(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                         const vfp_bus_t *bus);

/*
 * Why the last refused access came back, as a short human-readable phrase, or
 * NULL if the last vfp_execute did not trap. Unsupported instructions are also
 * printed to stderr; architectural guest exceptions are intentionally quiet.
 */
const char *vfp_trap_reason(void);

#endif /* S5LBOX_VFP_H */
