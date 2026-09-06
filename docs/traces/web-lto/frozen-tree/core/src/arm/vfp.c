/*
 * S5LBox — VFPv2 (the ARM1176JZF-S's VFP11 unit).
 *
 * WHY THIS EXISTS, AND WHY NOW
 * ----------------------------
 * The boot reaches launchd, which issues five syscalls and then executes a VFP
 * instruction. Our lazy-VFP Undefined trap vectors the guest to 0x04; XNU's
 * _sleh_undef (0xc006c184) recognises the encoding and calls _vfp_trap ->
 * _vfp_switch (0xc0069384), which sets FPEXC.EN and then UNCONDITIONALLY
 * executes
 *
 *     VLDMIA r1!, {s0-s31}          ; 0xecb10a20
 *
 * to restore the thread's VFP register file. There is no way past that
 * instruction without a real register file, so real VFP is a requirement the
 * kernel's own code established, not an assumption.
 *
 *
 * WHAT THE HARDWARE IS
 * --------------------
 * VFPv2. 32 single-precision registers s0-s31, aliased onto 16 double-precision
 * registers d0-d15; dN is the pair (s[2N] low word, s[2N+1] high word). There
 * is NO d16-d31 and NO Advanced SIMD/NEON on this part, so every encoding that
 * would name one is refused rather than silently folded onto a register that
 * does exist. Three system registers matter: FPSID (read-only identity),
 * FPSCR (status and control) and FPEXC (the enable, which is the whole lazy
 * per-thread enable mechanism — see the discrimination comment in
 * arm_interp.c).
 *
 *
 * =====================  FLOATING-POINT SEMANTICS  ==========================
 *
 * Arithmetic is the host's C `float` and `double`. That is IEEE-754 binary32
 * and binary64, which is the same format VFPv2 uses, so for round-to-nearest-
 * even every operation implemented here is bit-exact against hardware.
 *
 * MODELLED EXACTLY
 *   - the s/d register file and its aliasing
 *   - +, -, *, /, sqrt, and the four multiply-accumulate forms, each with its
 *     own rounding step (VFPv2's VMLA is NOT fused; see the note at f32_do)
 *   - comparisons, including the unordered case, writing FPSCR.NZCV
 *   - the integer and double/single conversions, with ARM's saturation rules
 *   - the cumulative exception flags IOC / DZC / OFC / UFC / IXC / IDC
 *   - FPSCR.FZ, flush-to-zero, on both the input and the output side; and
 *     FPSCR.DN, default-NaN. These are not optional decoration: dyld runs in
 *     ARM's "RunFast" configuration with FZ set, so the very first
 *     floating-point instruction launchd's dynamic linker executes is already
 *     in a non-default mode.
 *
 * REFUSED, NOT APPROXIMATED. Before any instruction that could be changed by
 * one of these, FPSCR is checked and the instruction TRAPS (ARM_UNDEFINED,
 * with a printed reason) rather than computing a plausible wrong number:
 *
 *   invalid short-vector shapes
 *                      VFP11 implements the legacy VFPv2 short-vector mode.
 *                      Valid LEN/STRIDE combinations execute with circular
 *                      register-bank addressing below. Reserved strides and
 *                      shapes that reuse a register are still refused rather
 *                      than assigned one of the architecture's Unpredictable
 *                      outcomes.
 *   FPSCR.RMode != 0   directed rounding (RP/RM/RZ). Host arithmetic rounds to
 *                      nearest-even. Honouring the other three would mean
 *                      driving the host's rounding mode through <fenv.h>
 *                      fesetround, which without a working #pragma STDC
 *                      FENV_ACCESS the optimiser is entitled to schedule
 *                      around — a wrong float that looks plausible. Only
 *                      instructions that actually round are gated on this;
 *                      VMOV, VABS, VNEG, VCMP, the exact widening conversions
 *                      and VCVT's explicit round-toward-zero form all run in
 *                      any rounding mode, because none of them consult it.
 *   FPSCR trap enables (IOE/DZE/OFE/UFE/IXE/IDE)
 *                      ask the unit to BOUNCE the instruction to support code
 *                      through FPEXC.EX/FPINST. We never enter exceptional
 *                      state, so honouring the request is not possible and
 *                      ignoring it would turn a signalled exception into a
 *                      silently-completed operation. Gated only where an
 *                      exception can actually arise.
 *   the flush-to-zero boundary
 *                      one case inside FZ that the host cannot answer; it is
 *                      detected and refused rather than guessed. See fz_out32.
 *
 * KNOWN DEVIATIONS (all of them, deliberately listed)
 *   1. NaN PAYLOAD PROPAGATION ORDER. With DN clear, VFP propagates an input
 *      NaN: a signalling NaN wins over a quiet one regardless of operand order,
 *      and is quieted by setting its top fraction bit. The host (x86 SSE, and
 *      arm64) instead prefers the FIRST operand. So an operation with a quiet
 *      NaN in operand 1 and a signalling NaN in operand 2 yields the same
 *      CLASS of result (a quiet NaN) and the same IOC flag, but possibly a
 *      different payload. Nothing in a boot depends on a NaN payload; code
 *      that did would be relying on behaviour ARM itself documents as
 *      implementation-specific across its own cores. With DN set — which is
 *      how dyld runs — this deviation cannot be observed at all.
 *   2. FPINST / FPINST2 (the bounce registers) and MVFR0 / MVFR1 are not
 *      implemented and reading them traps. We never bounce, so FPINST has no
 *      truthful value; and we have no verified MVFR reading for VFP11, so
 *      answering would be inventing a feature advertisement. XNU 1357 reads
 *      neither.
 *   3. x87 hosts would double-round single-precision arithmetic. Every host
 *      this builds for (x86-64 with SSE, arm64) computes binary32 natively.
 *   4. FPSID reports ARM1176_FPSID (0x410120b4), inherited from the CP15
 *      identity block in arm.h. The implementer, "VFPv2 subarchitecture" and
 *      part fields are certain; the variant/revision nibbles are not
 *      independently verified against an ARM1176JZF-S TRM here, and nothing in
 *      XNU 1357 branches on them.
 *
 * HOW THE EXCEPTION FLAGS ARE SAMPLED
 *   From the host, via <fenv.h>: feclearexcept before the operation and
 *   fetestexcept after. That is the only way to get UFC and IXC right without
 *   reimplementing rounding. Because GCC does not enable #pragma STDC
 *   FENV_ACCESS, the arithmetic is anchored between the two library calls with
 *   `volatile` staging variables: a volatile access is an observable side
 *   effect and cannot be reordered across a call to an opaque external
 *   function, so the operands are read after the clear and the result is
 *   written before the test. The same anchoring is what forces each rounding
 *   step of VMLA to be materialised, which stops -ffp-contract from fusing the
 *   multiply and the add into an FMA that VFPv2 does not have.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "vfp.h"

#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ===================================================================== trap */

static const char *g_reason;

const char *vfp_trap_reason(void) { return g_reason; }

/*
 * Refuse an encoding, loudly. Everything this unit cannot execute comes
 * through here, so a halt always names the encoding, the PC and the reason —
 * which is the whole point of this core's "trap what you don't implement"
 * rule. The default case of every switch below is a call to this, never a
 * fallthrough that computes something.
 */
static arm_status_t vfp_trap(uint32_t pc, uint32_t insn, const char *why) {
    g_reason = why;
    fprintf(stderr, "vfp: refusing 0x%08x at pc 0x%08x: %s\n", insn, pc, why);
    return ARM_UNDEFINED;
}

/* A defined access check failed, so hardware enters the guest's Undefined
 * exception. This is not an emulator capability gap and must never halt the
 * host process merely because user code executed a privileged VFP access. */
static arm_status_t vfp_guest_undefined(const char *why) {
    g_reason = why;
    return ARM_GUEST_UNDEFINED;
}

/* ======================================================== availability ==== *
 *
 * CPACR gates CP10/CP11 before FPEXC does. The ARM1176 requires the two fields
 * to be programmed identically (ARM DDI 0301H 3.2.7) and XNU's _init_vfp does
 * exactly that — "CPACR |= 0xf << 20" from _arm_init+0x2cc, the only CPACR
 * write anywhere in the kernel — so in practice they always agree. Where they
 * do not, take the more restrictive: a VFP instruction needs both halves, and
 * over-permitting would silently execute something the hardware would refuse.
 */
bool vfp_cpacr_permits(const arm_cpu_t *c) {
    unsigned cp10 = (c->cp15.cpacr >> ARM_CPACR_CP10_SHIFT) & 3u;
    unsigned cp11 = (c->cp15.cpacr >> ARM_CPACR_CP11_SHIFT) & 3u;
    unsigned acc  = cp10 < cp11 ? cp10 : cp11;
    if (acc == 3u) return true;                                /* full access */
    if (acc == 1u) return (c->cpsr & ARM_CPSR_MODE_MASK) != ARM_MODE_USR;
    return false;                                     /* 0 denied, 2 reserved */
}

bool vfp_enabled(const arm_cpu_t *c) {
    return (c->vfp_fpexc & ARM_FPEXC_EN) != 0;
}

/* ============================================== FPSCR mode admissibility ==
 *
 * Three tiers, from the semantics note at the top of this file. Each returns
 * the offending FPSCR bits, or 0 when the current mode is one we model
 * exactly. Splitting them matters: VMOV and VABS must keep working in RZ mode
 * because hardware does not consult the rounding mode for them, and refusing
 * would invent a fault the guest cannot possibly be expecting.
 */
/*
 * Three tiers, from the least to the most that an instruction can depend on.
 *
 * The trap enables gate every instruction that can raise an exception, which
 * VMOV, VABS and VNEG cannot. Short-vector LEN/STRIDE is handled by the CDP
 * operations themselves: arithmetic and unary operations can iterate, while
 * compares and conversions are architecturally scalar-only even when LEN is
 * nonzero.
 *
 * FPSCR.RMode used to be a third gate, refusing every rounding instruction in a
 * directed mode. It is no longer, because the mode is now implemented rather
 * than refused: vfp_execute() adopts it on the host FPU for the duration of an
 * instruction, and the float-to-integer path rounds explicitly in software via
 * fp_round_integral(). Refusing was defensible while nothing needed it, but
 * UIKit sets a directed mode and then runs whole sequences of conversions and
 * arithmetic under it on the way to SpringBoard's first frame.
 *
 * FZ and DN are not here because they are implemented (see fz_in32 and
 * f32_do); they change results rather than making them unrepresentable.
 */
#define MODE_EXACT    0u
#define MODE_VALUES   (ARM_FPSCR_ENABLES)
#define MODE_ROUNDING (MODE_VALUES)

static const char *mode_complaint(uint32_t bad) {
    if (bad & ARM_FPSCR_ENABLES) return "FPSCR enables a trapped FP exception";
    if (bad & ARM_FPSCR_RMODE)   return "FPSCR.RMode selects directed rounding";
    return "FPSCR selects an unmodelled mode";
}

/* ===================================================== bit-exact casts ==== */

static inline float    u2f(uint32_t u) { float  f; memcpy(&f, &u, 4); return f; }
static inline uint32_t f2u(float    f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline double   u2d(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline uint64_t d2u(double   d) { uint64_t u; memcpy(&u, &d, 8); return u; }

/* ARM's unary negations are sign-bit flips, not arithmetic: they neither
 * signal on a NaN nor change its payload. Doing them in the integer domain
 * makes that explicit. */
static inline float  fneg32(float  f) { return u2f(f2u(f) ^ 0x80000000u); }
static inline double fneg64(double d) { return u2d(d2u(d) ^ 0x8000000000000000ull); }
static inline float  fabs32(float  f) { return u2f(f2u(f) & 0x7fffffffu); }
static inline double fabs64(double d) { return u2d(d2u(d) & 0x7fffffffffffffffull); }

static inline bool snan32(uint32_t u) {
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0
        && (u & 0x00400000u) == 0;
}
static inline bool snan64(uint64_t u) {
    return (u & 0x7ff0000000000000ull) == 0x7ff0000000000000ull
        && (u & 0x000fffffffffffffull) != 0
        && (u & 0x0008000000000000ull) == 0;
}

/* ============================================ host arithmetic + flags ==== */

typedef enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_SQRT } fop_t;

/*
 * A private marker carried alongside the FPSCR exception bits. It is NOT an
 * FPSCR bit (6 is reserved on VFPv2) and never reaches FPSCR: seeing it means
 * flush-to-zero cannot be decided from the host's rounded result and the
 * instruction must trap. See fz_out32.
 */
#define VFP_FZ_AMBIGUOUS (1u << 6)

#define F32_MIN_NORMAL 0x00800000u
#define F64_MIN_NORMAL 0x0010000000000000ull
#define F32_DEFAULT_NAN 0x7fc00000u
#define F64_DEFAULT_NAN 0x7ff8000000000000ull

/* ------------------------------------------------- flush-to-zero, FPSCR.FZ --
 *
 * INPUT side (ARM ARM FPUnpack): a denormal operand is replaced by a zero of
 * the same sign and IDC is set. The sign is preserved because FPUnpack reads
 * it before it decides the operand is a zero, which is what makes
 * denormal * -1 come out negative.
 *
 * OUTPUT side (ARM ARM FPRound): a result whose magnitude is below the
 * smallest normal is replaced by a zero of the result's sign and UFC is set —
 * and the inexact report is NOT also made, because the flush replaces the
 * rounding rather than following it.
 *
 * THE ONE CASE THIS CANNOT DECIDE. The architecture tests the result BEFORE
 * rounding; the host hands us the result AFTER. Every disagreement is
 * therefore confined to one situation: the host returned exactly the smallest
 * normal AND the operation was inexact, meaning the exact result sat in the
 * half-ulp sliver on one side or the other of that boundary. Below it, the
 * architecture flushes to zero; above it, it does not; and the host's answer
 * looks identical either way. Rather than pick, that case traps and names
 * itself. It is detected, not hoped about.
 */
static float fz_in32(float f, uint32_t *exc) {
    uint32_t u = f2u(f);
    if ((u & 0x7f800000u) == 0u && (u & 0x007fffffu) != 0u) {
        *exc |= ARM_FPSCR_IDC;
        return u2f(u & 0x80000000u);
    }
    return f;
}
static double fz_in64(double d, uint32_t *exc) {
    uint64_t u = d2u(d);
    if ((u & 0x7ff0000000000000ull) == 0ull &&
        (u & 0x000fffffffffffffull) != 0ull) {
        *exc |= ARM_FPSCR_IDC;
        return u2d(u & 0x8000000000000000ull);
    }
    return d;
}
static float fz_out32(float f, uint32_t *exc) {
    uint32_t u = f2u(f), mag = u & 0x7fffffffu;
    if (mag != 0u && mag < F32_MIN_NORMAL) {
        *exc = (*exc & ~ARM_FPSCR_IXC) | ARM_FPSCR_UFC;
        return u2f(u & 0x80000000u);
    }
    if (mag == F32_MIN_NORMAL && (*exc & ARM_FPSCR_IXC)) *exc |= VFP_FZ_AMBIGUOUS;
    return f;
}
static double fz_out64(double d, uint32_t *exc) {
    uint64_t u = d2u(d), mag = u & 0x7fffffffffffffffull;
    if (mag != 0ull && mag < F64_MIN_NORMAL) {
        *exc = (*exc & ~ARM_FPSCR_IXC) | ARM_FPSCR_UFC;
        return u2d(u & 0x8000000000000000ull);
    }
    if (mag == F64_MIN_NORMAL && (*exc & ARM_FPSCR_IXC)) *exc |= VFP_FZ_AMBIGUOUS;
    return d;
}

/* Default-NaN mode, FPSCR.DN: every NaN result becomes the one default quiet
 * NaN instead of a propagated input. Exact, and it happens to make the NaN
 * payload deviation noted at the top of this file unobservable. */
static float dn_out32(float f, uint32_t fpscr) {
    if ((fpscr & ARM_FPSCR_DN) && (f2u(f) & 0x7fffffffu) > 0x7f800000u)
        return u2f(F32_DEFAULT_NAN);
    return f;
}
static double dn_out64(double d, uint32_t fpscr) {
    if ((fpscr & ARM_FPSCR_DN) &&
        (d2u(d) & 0x7fffffffffffffffull) > 0x7ff0000000000000ull)
        return u2d(F64_DEFAULT_NAN);
    return d;
}

/*
 * READING THE HOST'S STICKY FLAGS, CHEAPLY.
 *
 * <fenv.h> is the portable way and it was the only way here until it was
 * priced. feclearexcept()/fetestexcept() are opaque library calls, and this
 * unit makes BOTH of them around every single arithmetic operation, so a guest
 * multiply costs two function calls plus a multiply. tools/insnbench.c row
 * "vfp mul/add/cvt" measures what that is worth: 3.59 M insn/s with them
 * against 15.36 without, on a host managing 19.06 for integer work. The fenv
 * calls, not the arithmetic and not the volatile staging, are essentially the
 * entire cost of emulating VFP.
 *
 * The flags live in one register on every host this runs on, so the fast paths
 * below read and clear that register directly. SEMANTICS ARE UNCHANGED: the
 * same flags are sampled at the same two points around the same operation.
 * This is not the deferred-harvest trick, which would be faster still and
 * would be wrong -- any host floating point performed between two guest
 * instructions would leak into the guest's FPSCR, and FE_INEXACT is raised by
 * almost every operation, so that leak would not be rare.
 *
 * Portability is preserved by construction: an unrecognised host keeps the
 * <fenv.h> path, which is still correct, merely slower. i386 deliberately does
 * NOT take the SSE path -- a 32-bit x86 build may carry float arithmetic in
 * x87, whose flags are in a different register entirely (see the double
 * rounding note at the top of this file).
 */
#if defined(__x86_64__) || defined(_M_X64)
#  include <xmmintrin.h>
#  define VFP_HOST_FLAGS "mxcsr"
/* MXCSR exception flags: IE0 DE1 ZE2 OE3 UE4 PE5. DE (denormal) has no ARM
 * equivalent among the cumulative bits and is deliberately dropped. */
static inline void host_exceptions_clear(void) {
    _mm_setcsr(_mm_getcsr() & ~0x3fu);
}
static inline uint32_t host_exceptions(void) {
    unsigned csr = _mm_getcsr();
    uint32_t f = 0;
    if (csr & (1u << 0)) f |= ARM_FPSCR_IOC;
    if (csr & (1u << 2)) f |= ARM_FPSCR_DZC;
    if (csr & (1u << 3)) f |= ARM_FPSCR_OFC;
    if (csr & (1u << 4)) f |= ARM_FPSCR_UFC;
    if (csr & (1u << 5)) f |= ARM_FPSCR_IXC;
    return f;
}
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#  define VFP_HOST_FLAGS "fpsr"
/*
 * ARMv8 FPSR carries IOC/DZC/OFC/UFC/IXC in bits 0-4, which is exactly where
 * ARMv6 FPSCR carries them, so the mask IS the translation. This is the path
 * the iPhone takes.
 */
#  define VFP_FPSR_CUMULATIVE 0x1fu
static inline uint32_t vfp_read_fpsr(void) {
    uint64_t v;
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(v));
    return (uint32_t)v;
}
static inline void host_exceptions_clear(void) {
    uint64_t v;
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(v));
    v &= ~(uint64_t)VFP_FPSR_CUMULATIVE;
    __asm__ __volatile__("msr fpsr, %0" :: "r"(v));
}
static inline uint32_t host_exceptions(void) {
    return vfp_read_fpsr() & VFP_FPSR_CUMULATIVE;
}
#elif defined(__EMSCRIPTEN__)
/*
 * Emscripten's WebAssembly floating-point environment has no portable
 * exception-status API. WebAssembly arithmetic is still IEEE-754, but the
 * host exception flags cannot be observed here; leave the cumulative flags
 * clear rather than depending on unavailable fenv constants.
 */
#  define VFP_HOST_FLAGS "wasm"
static inline void host_exceptions_clear(void) {}
static inline uint32_t host_exceptions(void) { return 0; }
#else
#  define VFP_HOST_FLAGS "fenv"
static inline void host_exceptions_clear(void) {
    feclearexcept(FE_ALL_EXCEPT);
}
static uint32_t host_exceptions(void) {
    int raised = fetestexcept(FE_ALL_EXCEPT);
    uint32_t f = 0;
    if (raised & FE_INVALID)   f |= ARM_FPSCR_IOC;
    if (raised & FE_DIVBYZERO) f |= ARM_FPSCR_DZC;
    if (raised & FE_OVERFLOW)  f |= ARM_FPSCR_OFC;
    if (raised & FE_UNDERFLOW) f |= ARM_FPSCR_UFC;
    if (raised & FE_INEXACT)   f |= ARM_FPSCR_IXC;
    return f;
}
#endif

/*
 * One rounding step, with its exception flags captured.
 *
 * The volatile staging is load-bearing, twice over. It anchors the arithmetic
 * between feclearexcept and fetestexcept (see the header comment), and it
 * forces the result of each call to be materialised at binary32/binary64
 * width, which is what makes a sequence of two calls round twice. VFPv2 has no
 * fused multiply-add: VMLA is a multiply, rounded, then an add, rounded, and
 * an optimiser that contracted the two would give an answer that differs from
 * hardware in the last bit — the exact class of silent wrongness this core
 * exists to avoid.
 */
static float f32_do(fop_t op, float a, float b, uint32_t fpscr, uint32_t *exc) {
    volatile float va, vb, vr;
    float x, y, r;
    uint32_t e = 0;

    if (fpscr & ARM_FPSCR_FZ) { a = fz_in32(a, &e); b = fz_in32(b, &e); }
    va = a; vb = b;
    host_exceptions_clear();
    x = va; y = vb;
    switch (op) {
        case OP_ADD: r = x + y;    break;
        case OP_SUB: r = x - y;    break;
        case OP_MUL: r = x * y;    break;
        case OP_DIV: r = x / y;    break;
        default:
            /* VSQRT of a negative operand other than -0 is an Invalid
             * Operation: IOC must be set and the result is the Default
             * NaN. The host's sqrt does not reliably raise FE_INVALID
             * for this, so raise it explicitly rather than depending on
             * a libm detail. -0 and NaN both compare false here, which
             * is correct: sqrt(-0) is -0 and quiet NaNs propagate.  */
            if (x < 0.0f) e |= ARM_FPSCR_IOC;
            r = sqrtf(x);
            break;
    }
    vr = r;
    e |= host_exceptions();
    r  = vr;
    if (fpscr & ARM_FPSCR_FZ) r = fz_out32(r, &e);
    *exc |= e;
    return dn_out32(r, fpscr);
}

static double f64_do(fop_t op, double a, double b, uint32_t fpscr, uint32_t *exc) {
    volatile double va, vb, vr;
    double x, y, r;
    uint32_t e = 0;

    if (fpscr & ARM_FPSCR_FZ) { a = fz_in64(a, &e); b = fz_in64(b, &e); }
    va = a; vb = b;
    host_exceptions_clear();
    x = va; y = vb;
    switch (op) {
        case OP_ADD: r = x + y;   break;
        case OP_SUB: r = x - y;   break;
        case OP_MUL: r = x * y;   break;
        case OP_DIV: r = x / y;   break;
        default:
            /* VSQRT of a negative operand other than -0 is an Invalid
             * Operation: IOC must be set and the result is the Default
             * NaN. The host's sqrt does not reliably raise FE_INVALID
             * for this, so raise it explicitly rather than depending on
             * a libm detail. -0 and NaN both compare false here, which
             * is correct: sqrt(-0) is -0 and quiet NaNs propagate.  */
            if (x < 0.0) e |= ARM_FPSCR_IOC;
            r = sqrt(x);
            break;
    }
    vr = r;
    e |= host_exceptions();
    r  = vr;
    if (fpscr & ARM_FPSCR_FZ) r = fz_out64(r, &e);
    *exc |= e;
    return dn_out64(r, fpscr);
}

/*
 * Round to an integral value the way FPSCR.RMode says to.
 *
 * This is worth doing explicitly rather than delegating to the host FPU. A
 * conversion is the one place the emulator can honour a directed rounding mode
 * exactly and cheaply, because the rounding happens here in software; the
 * arithmetic operations cannot, because they hand the value to the host FPU in
 * whatever mode the host is in, which is why those still refuse a directed
 * mode instead of quietly rounding the wrong way.
 *
 * RN is left to rint() in the host's default mode, which is round-to-nearest
 * ties-to-even, matching the architecture. The emulator never changes the host
 * mode, so that equivalence holds.
 */
static double fp_round_integral(double v, uint32_t fpscr) {
    switch ((fpscr & ARM_FPSCR_RMODE) >> 22) {
    case 1u:  return ceil(v);                    /* RP: toward +infinity */
    case 2u:  return floor(v);                   /* RM: toward -infinity */
    case 3u:  return trunc(v);                   /* RZ: toward zero      */
    default:  return rint(v);                    /* RN: nearest, ties even */
    }
}

/*
 * ARM ARM FPToFixed with fbits == 0. NaN converts to zero and sets IOC; an
 * out-of-range value saturates to the extreme of the destination type and sets
 * IOC (and NOT IXC — saturation replaces the inexact report, it does not
 * accompany it). Rounding is explicit here rather than delegated to the host's
 * mode: round_to_zero is VCVT's architectural truncation, and the other case
 * is VCVTR, which takes FPSCR.RMode through fp_round_integral().
 */
static uint32_t fp_to_int(double v, bool is_signed, bool round_to_zero,
                          uint32_t fpscr, uint32_t *exc) {
    double r;
    if (v != v) { *exc |= ARM_FPSCR_IOC; return 0; }      /* NaN -> 0 + IOC */
    r = round_to_zero ? trunc(v) : fp_round_integral(v, fpscr);
    if (is_signed) {
        if (r >=  2147483648.0) { *exc |= ARM_FPSCR_IOC; return 0x7fffffffu; }
        if (r <  -2147483648.0) { *exc |= ARM_FPSCR_IOC; return 0x80000000u; }
        if (r != v) *exc |= ARM_FPSCR_IXC;
        return (uint32_t)(int32_t)r;
    }
    if (r >= 4294967296.0) { *exc |= ARM_FPSCR_IOC; return 0xffffffffu; }
    if (r <  0.0)          { *exc |= ARM_FPSCR_IOC; return 0u; }
    if (r != v) *exc |= ARM_FPSCR_IXC;
    return (uint32_t)r;
}

/* The four comparison flags, ARM ARM FPCompare. Note these land in FPSCR, not
 * CPSR: the guest moves them across with "VMRS APSR_nzcv, FPSCR". */
static uint32_t cmp_flags_ordered(int order) {
    switch (order) {
        case -1: return ARM_FPSCR_N;                        /* less than     */
        case  0: return ARM_FPSCR_Z | ARM_FPSCR_C;          /* equal         */
        case  1: return ARM_FPSCR_C;                        /* greater than  */
        default: return ARM_FPSCR_C | ARM_FPSCR_V;          /* unordered     */
    }
}

/* ================================================== register numbering ==== */

/*
 * The D/N/M bits (23:22 is D, 7 is N, 5 is M) are the LOW bit of a
 * single-precision register number and the HIGH bit of a double-precision one.
 * VFPv2 has only d0-d15, so a set high bit names a register the ARM1176 does
 * not have; refusing is the only truthful answer, and it is also the one that
 * catches a decode mistake of ours instead of aliasing it onto d0-d15.
 */
#define BIT(i)   ((insn >> (i)) & 1u)
#define FIELD(hi) ((insn >> (hi)) & 0xfu)
#define SREG(f4, lo) (((f4) << 1) | (lo))

/* ------------------------------------------------ legacy short vectors --
 *
 * VFP11 divides the register file into four circular banks: eight S registers
 * or four D registers per bank. A nonzero LEN makes a data-processing result
 * a vector only when its destination is outside bank zero. Fn then advances as
 * a vector, while Fm advances only when it too is outside bank zero; an Fm in
 * bank zero is broadcast as a scalar. The destination being in bank zero
 * deliberately leaves the whole operation scalar, even while LEN stays set.
 *
 * ARM DDI 0274H, sections 2.7 and 3.4.2, defines only stride encodings 00
 * (one) and 11 (two). It also calls a shape Unpredictable when wrapping would
 * visit one register twice. Refusing those shapes is this emulator's normal
 * fail-closed policy for Unpredictable encodings; valid shapes are exact.
 */
typedef struct {
    unsigned count;
    unsigned stride;
    unsigned bank_mask;
    unsigned bank_size;
    bool vector;
} vfp_short_vector_t;

static bool vfp_short_vector_shape(uint32_t fpscr, bool dbl, unsigned rd,
                                   vfp_short_vector_t *shape,
                                   const char **why) {
    const unsigned len = (fpscr & ARM_FPSCR_LEN) >> 16;
    const unsigned encoded_stride = (fpscr & ARM_FPSCR_STRIDE) >> 20;
    const unsigned bank_size = dbl ? 4u : 8u;

    if (!shape) return false;
    shape->count = 1u;
    shape->stride = 1u;
    shape->bank_mask = bank_size - 1u;
    shape->bank_size = bank_size;
    shape->vector = len != 0u && rd >= bank_size;

    if (len == 0u) {
        if (encoded_stride != 0u) {
            if (why) *why = "FPSCR.Stride is invalid when vector length is one";
            return false;
        }
        return true;
    }
    if (encoded_stride == 0u)
        shape->stride = 1u;
    else if (encoded_stride == 3u)
        shape->stride = 2u;
    else {
        if (why) *why = "FPSCR.Stride uses a reserved encoding";
        return false;
    }

    /* A bank-zero destination makes this instruction scalar regardless of
     * LEN. Its operands are therefore used once and cannot wrap onto
     * themselves. */
    if (!shape->vector) return true;

    shape->count = len + 1u;
    if (shape->count > bank_size / shape->stride) {
        if (why) *why = "FPSCR.Len/Stride would reuse a short-vector register";
        return false;
    }
    return true;
}

static unsigned vfp_short_vector_reg(const vfp_short_vector_t *shape,
                                     unsigned reg, unsigned lane,
                                     bool bank_zero_is_scalar) {
    if (!shape || !shape->vector ||
        (bank_zero_is_scalar && reg < shape->bank_size))
        return reg;
    return (reg & ~shape->bank_mask) |
           ((reg + lane * shape->stride) & shape->bank_mask);
}

/* ================================================= load / store group ==== *
 *
 * cond 110 P U D W L Rn Vd 101 sz imm8   (ARM ARM A7.6, "Extension register
 * load/store"). The five bits P:U:D:W:L select the form:
 *
 *   0b0000x  UNDEFINED            0b10x0x  VLDR/VSTR, negative offset
 *   0b0010x  64-bit core transfer 0b10x1x  VLDM/VSTM decrement-before, wb
 *   0b01x0x  VLDM/VSTM IA, no wb  0b11x0x  VLDR/VSTR, positive offset
 *   0b01x1x  VLDM/VSTM IA, wb     0b11x1x  UNDEFINED
 *
 * VPUSH and VPOP are not separate instructions: they are the decrement-before
 * and increment-after writeback forms with Rn == sp, and fall out of this
 * decode for free. _vfp_switch's VLDMIA r1!, {s0-s31} is the 0b01x11 row.
 */
static arm_status_t vfp_ldst(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                             const vfp_bus_t *bus) {
    bool     P = BIT(24), U = BIT(23), D = BIT(22), W = BIT(21), L = BIT(20);
    unsigned rn = FIELD(16), vd = FIELD(12);
    bool     dbl = BIT(8);
    unsigned imm8 = insn & 0xffu;
    uint32_t base, addr;
    unsigned first, count, i;

    if (!P && !U && !W) return vfp_trap(pc, insn, "UNDEFINED extension load/store form");
    if (P && U && W)    return vfp_trap(pc, insn, "UNDEFINED extension load/store form");

    /* Rn == 15 is the PC-relative literal form of VLDR/VSTR only; with
     * writeback, or as a VLDM/VSTM base, the architecture calls it
     * UNPREDICTABLE and we will not guess which of the two plausible
     * behaviours a given core picked. */
    if (rn == 15u) {
        if (W || !P) return vfp_trap(pc, insn, "PC as a writeback/VLDM base is UNPREDICTABLE");
        base = pc + 8u;                                    /* Align(PC,4)    */
    } else {
        base = c->r[rn];
    }

    /* ---- VLDR / VSTR: a single register at an immediate word offset. ---- */
    if (P && !W) {
        addr = U ? base + imm8 * 4u : base - imm8 * 4u;
        if (dbl) {
            if (D) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
            if (L) {
                uint32_t lo = bus->read32(c, addr);
                if (c->abort_pending) return ARM_OK;
                uint32_t hi = bus->read32(c, addr + 4u);
                if (!c->abort_pending)
                    vfp_set_d(c, vd, (uint64_t)lo | ((uint64_t)hi << 32));
            } else {
                uint64_t v = vfp_get_d(c, vd);
                bus->write32(c, addr,      (uint32_t)v);
                if (c->abort_pending) return ARM_OK;
                bus->write32(c, addr + 4u, (uint32_t)(v >> 32));
            }
        } else {
            unsigned sd = SREG(vd, D);
            if (L) {
                uint32_t v = bus->read32(c, addr);
                if (!c->abort_pending) vfp_set_s(c, sd, v);
            } else {
                bus->write32(c, addr, vfp_get_s(c, sd));
            }
        }
        return ARM_OK;
    }

    /* ---- VLDM / VSTM (and therefore VPUSH / VPOP). ---------------------- */
    if (imm8 == 0u) return vfp_trap(pc, insn, "VLDM/VSTM with an empty register list");

    if (dbl) {
        if (D) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
        first = vd;
        count = imm8 / 2u;
        /*
         * An ODD imm8 is the deprecated VFPv2 FLDMX/FSTMX format: it moves
         * imm8/2 doublewords and reserves one extra word whose contents are
         * unspecified. The reservation is honoured (the transfer occupies
         * imm8*4 bytes either way, so writeback lands in the right place); the
         * extra word reads as ignored and writes as zero, which is within what
         * the architecture leaves UNKNOWN.
         */
        if (first + count > 16u)
            return vfp_trap(pc, insn, "register list runs past d15");
    } else {
        first = SREG(vd, D);
        count = imm8;
        if (first + count > 32u)
            return vfp_trap(pc, insn, "register list runs past s31");
    }

    addr = U ? base : base - imm8 * 4u;

    for (i = 0; i < count; i++) {
        if (dbl) {
            if (L) {
                uint32_t lo = bus->read32(c, addr + i * 8u);
                if (c->abort_pending) return ARM_OK;
                uint32_t hi = bus->read32(c, addr + i * 8u + 4u);
                if (c->abort_pending) return ARM_OK;
                vfp_set_d(c, first + i, (uint64_t)lo | ((uint64_t)hi << 32));
            } else {
                uint64_t v = vfp_get_d(c, first + i);
                bus->write32(c, addr + i * 8u,      (uint32_t)v);
                if (c->abort_pending) return ARM_OK;
                bus->write32(c, addr + i * 8u + 4u, (uint32_t)(v >> 32));
                if (c->abort_pending) return ARM_OK;
            }
        } else {
            if (L) {
                uint32_t v = bus->read32(c, addr + i * 4u);
                if (c->abort_pending) return ARM_OK;
                vfp_set_s(c, first + i, v);
            } else {
                bus->write32(c, addr + i * 4u, vfp_get_s(c, first + i));
                if (c->abort_pending) return ARM_OK;
            }
        }
    }
    /*
     * FLDMX/FSTMX's reserved trailing word. It is still touched — the transfer
     * really does span imm8*4 bytes, so a list that straddles a page boundary
     * must fault the same way hardware would — but its value is architecturally
     * UNKNOWN, so it is discarded on load and written as zero on store.
     */
    if (dbl && (imm8 & 1u)) {
        if (L) (void)bus->read32(c, addr + count * 8u);
        else   bus->write32(c, addr + count * 8u, 0u);
        if (c->abort_pending) return ARM_OK;
    }

    if (W) c->r[rn] = U ? base + imm8 * 4u : base - imm8 * 4u;
    return ARM_OK;
}

/* ======================================== 32-bit core-register transfer ==
 *
 * cond 1110 opc1 L Vn Rt 101 sz N 0 0 1 0000 — the MCR/MRC form.
 *   cp10, opc1 == 000 : VMOV between Sn and Rt
 *   cp11, opc1 == 000 : VMOV between Dn[31:0] and Rt (FMDLR/FMRDL)
 *   cp11, opc1 == 001 : VMOV between Dn[63:32] and Rt (FMDHR/FMRDH)
 *   cp10, opc1 == 111 : VMSR / VMRS
 *
 * The cp11 word transfers are VFPv2 instructions on VFP11, despite modern
 * disassemblers spelling them with the same lane syntax used by NEON. Only
 * the 32-bit low/high halves exist here; the 8/16-bit scalar forms are NEON.
 */
static arm_status_t vfp_xfer32(arm_cpu_t *c, uint32_t pc, uint32_t insn) {
    unsigned opc1 = (insn >> 21) & 7u;
    bool     L    = BIT(20);
    bool     cp11 = BIT(8);
    unsigned vn   = FIELD(16), rt = FIELD(12);

    if (!cp11 && opc1 == 0u) {                      /* VMOV Sn <-> Rt        */
        unsigned sn = SREG(vn, BIT(7));
        if ((insn & 0x0000006fu) != 0u)
            return vfp_trap(pc, insn, "reserved bits set in VMOV (core register)");
        if (rt == 15u) return vfp_trap(pc, insn, "PC as VMOV core register is UNPREDICTABLE");
        if (L) c->r[rt] = vfp_get_s(c, sn);
        else   vfp_set_s(c, sn, c->r[rt]);
        return ARM_OK;
    }

    if (cp11 && opc1 <= 1u) {                       /* VMOV Dn word <-> Rt   */
        unsigned sn;
        if (BIT(7)) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
        if ((insn & 0x0000006fu) != 0u)
            return vfp_trap(pc, insn,
                "not a VFPv2 32-bit double-register word transfer");
        if (rt == 15u) return vfp_trap(pc, insn, "PC as VMOV core register is UNPREDICTABLE");
        sn = SREG(vn, opc1);                        /* Dn[31:0]/[63:32] */
        if (L) c->r[rt] = vfp_get_s(c, sn);
        else   vfp_set_s(c, sn, c->r[rt]);
        return ARM_OK;
    }

    if (cp11)
        return vfp_trap(pc, insn, "Advanced SIMD scalar transfer (no NEON on VFP11)");
    if (opc1 != 7u)
        return vfp_trap(pc, insn, "UNDEFINED VFPv2 32-bit core-register transfer");
    if ((insn & 0x000000efu) != 0u)
        return vfp_trap(pc, insn, "reserved bits set in VMRS/VMSR");
    if (vn == 8u && (c->cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR)
        return vfp_guest_undefined("FPEXC is privileged");
    if (vn == 0u && !vfp_enabled(c) &&
        (c->cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR)
        return vfp_guest_undefined("FPSID is privileged while VFP is disabled");

    /*
     * VMRS / VMSR. The availability asymmetry here IS the lazy-enable
     * mechanism: with FPEXC.EN clear the only accessible system registers are
     * FPEXC (8) and FPSID (0), both from privileged mode. That is exactly what
     * _get_vfp_enabled relies on when it reads FPEXC while VFP is off. The
     * caller has already applied the enable half of that rule.
     */
    if (L) {
        uint32_t v;
        if (rt == 15u && vn != 1u)
            return vfp_trap(pc, insn,
                "Rt=PC is defined only for VMRS APSR_nzcv, FPSCR");
        switch (vn) {
            case 0: v = ARM1176_FPSID;  break;
            case 1: v = c->vfp_fpscr;   break;
            case 8: v = c->vfp_fpexc;   break;
            default:
                return vfp_trap(pc, insn,
                    "VMRS of a VFP system register this unit does not implement "
                    "(MVFR0/MVFR1/FPINST/FPINST2)");
        }
        /* Rt == 15 is "VMRS APSR_nzcv, FPSCR": the FP comparison flags are
         * copied into CPSR so a following ARM conditional branch can test
         * them. It is the only defined use of Rt == 15 in this space. */
        if (rt == 15u) c->cpsr = (c->cpsr & 0x0fffffffu) | (v & 0xf0000000u);
        else           c->r[rt] = v;
        return ARM_OK;
    }

    if (rt == 15u) return vfp_trap(pc, insn, "PC as a VMSR source is UNPREDICTABLE");
    switch (vn) {
        /* Reserved FPSCR bits read as zero on the ARM1176, so they are dropped
         * on the way in rather than stored and handed back. */
        case 1: c->vfp_fpscr = c->r[rt] & ARM_FPSCR_WMASK; return ARM_OK;
        case 8: c->vfp_fpexc = c->r[rt];                   return ARM_OK;
        case 0: return vfp_trap(pc, insn, "FPSID is read-only");
        default:
            return vfp_trap(pc, insn,
                "VMSR of a VFP system register this unit does not implement");
    }
}

/* ======================================== 64-bit core-register transfer ==
 *
 * cond 1100 010 L Rt2 Rt 101 sz 00 M 1 Vm — two core registers to or from
 * either a pair of single registers or one double register.
 */
static arm_status_t vfp_xfer64(arm_cpu_t *c, uint32_t pc, uint32_t insn) {
    bool     L   = BIT(20), dbl = BIT(8), M = BIT(5);
    unsigned rt2 = FIELD(16), rt = FIELD(12), vm = insn & 0xfu;

    if ((insn & 0x000000c0u) != 0u)
        return vfp_trap(pc, insn, "reserved bits set in VMOV (two core registers)");
    if (rt == 15u || rt2 == 15u)
        return vfp_trap(pc, insn, "PC as a VMOV core register is UNPREDICTABLE");

    if (dbl) {
        if (M) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
        if (L) {
            uint64_t v = vfp_get_d(c, vm);
            c->r[rt]  = (uint32_t)v;
            c->r[rt2] = (uint32_t)(v >> 32);
        } else {
            vfp_set_d(c, vm, (uint64_t)c->r[rt] | ((uint64_t)c->r[rt2] << 32));
        }
        return ARM_OK;
    }

    {
        unsigned sm = SREG(vm, M);
        if (sm == 31u) return vfp_trap(pc, insn, "register pair runs past s31");
        if (L) {
            c->r[rt]  = vfp_get_s(c, sm);
            c->r[rt2] = vfp_get_s(c, sm + 1u);
        } else {
            vfp_set_s(c, sm,      c->r[rt]);
            vfp_set_s(c, sm + 1u, c->r[rt2]);
        }
    }
    return ARM_OK;
}

/* ================================================== data processing ====== */

enum {
    A_VMLA, A_VMLS, A_VNMLS, A_VNMLA, A_VMUL, A_VNMUL, A_VADD, A_VSUB, A_VDIV
};

#define FZ_AMBIGUOUS_WHY                                                      \
    "flush-to-zero cannot be decided: the exact result straddles the smallest " \
    "normal, and the architecture tests it before rounding while the host " \
    "reports it after"

/* The three-operand arithmetic group: opc1 (bits 23,21,20) selects the family
 * and opc3<0> (bit 6) the variant. Bit 22 is D, not part of the opcode. */
static arm_status_t vfp_dp_arith(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                                 unsigned op) {
    bool     dbl = BIT(8), alt = BIT(6);
    unsigned kind, rd, rn, rm;
    uint32_t exc = 0, bad;
    vfp_short_vector_t shape;
    const char *why = NULL;

    switch (op) {
        case 0: kind = alt ? A_VMLS  : A_VMLA; break;
        case 1: kind = alt ? A_VNMLA : A_VNMLS; break;
        case 2: kind = alt ? A_VNMUL : A_VMUL; break;
        case 3: kind = alt ? A_VSUB  : A_VADD; break;
        case 4:
            if (alt) return vfp_trap(pc, insn, "UNDEFINED VFP data-processing opcode");
            kind = A_VDIV;
            break;
        default:
            return vfp_trap(pc, insn,
                "VFPv4 fused multiply-accumulate; the VFP11 has no FMA");
    }

    bad = c->vfp_fpscr & MODE_ROUNDING;
    if (bad) return vfp_trap(pc, insn, mode_complaint(bad));

    if (dbl) {
        if (BIT(22) || BIT(7) || BIT(5))
            return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
        rd = FIELD(12); rn = FIELD(16); rm = insn & 0xfu;
    } else {
        rd = SREG(FIELD(12), BIT(22));
        rn = SREG(FIELD(16), BIT(7));
        rm = SREG(insn & 0xfu, BIT(5));
    }
    if (!vfp_short_vector_shape(c->vfp_fpscr, dbl, rd, &shape, &why))
        return vfp_trap(pc, insn, why);

    if (dbl) {
        uint32_t fs = c->vfp_fpscr;
        uint64_t result[4];
        for (unsigned lane = 0; lane < shape.count; lane++) {
            unsigned dr = vfp_short_vector_reg(&shape, rd, lane, false);
            unsigned nr = vfp_short_vector_reg(&shape, rn, lane, false);
            unsigned mr = vfp_short_vector_reg(&shape, rm, lane, true);
            double n = u2d(vfp_get_d(c, nr));
            double m = u2d(vfp_get_d(c, mr));
            double r;
            switch (kind) {
                case A_VADD:  r = f64_do(OP_ADD, n, m, fs, &exc); break;
                case A_VSUB:  r = f64_do(OP_SUB, n, m, fs, &exc); break;
                case A_VMUL:  r = f64_do(OP_MUL, n, m, fs, &exc); break;
                case A_VNMUL: r = fneg64(f64_do(OP_MUL, n, m, fs, &exc)); break;
                case A_VDIV:  r = f64_do(OP_DIV, n, m, fs, &exc); break;
                default: {
                    double d = u2d(vfp_get_d(c, dr));
                    double p = f64_do(OP_MUL, n, m, fs, &exc); /* round, then... */
                    if (kind == A_VMLS  || kind == A_VNMLA) p = fneg64(p);
                    if (kind == A_VNMLA || kind == A_VNMLS) d = fneg64(d);
                    r = f64_do(OP_ADD, d, p, fs, &exc);        /* ...round again */
                    break;
                }
            }
            result[lane] = d2u(r);
        }
        if (exc & VFP_FZ_AMBIGUOUS) return vfp_trap(pc, insn, FZ_AMBIGUOUS_WHY);
        for (unsigned lane = 0; lane < shape.count; lane++)
            vfp_set_d(c, vfp_short_vector_reg(&shape, rd, lane, false),
                      result[lane]);
    } else {
        uint32_t fs = c->vfp_fpscr;
        uint32_t result[8];
        for (unsigned lane = 0; lane < shape.count; lane++) {
            unsigned dr = vfp_short_vector_reg(&shape, rd, lane, false);
            unsigned nr = vfp_short_vector_reg(&shape, rn, lane, false);
            unsigned mr = vfp_short_vector_reg(&shape, rm, lane, true);
            float n = u2f(vfp_get_s(c, nr));
            float m = u2f(vfp_get_s(c, mr));
            float r;
            switch (kind) {
                case A_VADD:  r = f32_do(OP_ADD, n, m, fs, &exc); break;
                case A_VSUB:  r = f32_do(OP_SUB, n, m, fs, &exc); break;
                case A_VMUL:  r = f32_do(OP_MUL, n, m, fs, &exc); break;
                case A_VNMUL: r = fneg32(f32_do(OP_MUL, n, m, fs, &exc)); break;
                case A_VDIV:  r = f32_do(OP_DIV, n, m, fs, &exc); break;
                default: {
                    float d = u2f(vfp_get_s(c, dr));
                    float p = f32_do(OP_MUL, n, m, fs, &exc);
                    if (kind == A_VMLS  || kind == A_VNMLA) p = fneg32(p);
                    if (kind == A_VNMLA || kind == A_VNMLS) d = fneg32(d);
                    r = f32_do(OP_ADD, d, p, fs, &exc);
                    break;
                }
            }
            result[lane] = f2u(r);
        }
        if (exc & VFP_FZ_AMBIGUOUS) return vfp_trap(pc, insn, FZ_AMBIGUOUS_WHY);
        for (unsigned lane = 0; lane < shape.count; lane++)
            vfp_set_s(c, vfp_short_vector_reg(&shape, rd, lane, false),
                      result[lane]);
    }
    c->vfp_fpscr |= exc;
    return ARM_OK;
}

/*
 * The "other" group: opc1 == 1x11 with opc3<0> == 1, keyed by opc2 (bits
 * 19:16) and opc3 (bits 7:6). This is where the unary operations, the
 * comparisons and every conversion live.
 */
static arm_status_t vfp_dp_other(arm_cpu_t *c, uint32_t pc, uint32_t insn) {
    unsigned opc2 = FIELD(16);
    bool     dbl  = BIT(8), top = BIT(7);   /* top == opc3<1> */
    unsigned D = BIT(22), M = BIT(5);
    unsigned vd = FIELD(12), vm = insn & 0xfu;
    uint32_t exc = 0, bad;

    switch (opc2) {

    /* ---- VMOV (register), VABS, VNEG, VSQRT: same width in and out. ---- */
    case 0u: case 1u: {
        unsigned rd, rm;
        bool sqrt_ = (opc2 == 1u) && top;
        vfp_short_vector_t shape;
        const char *why = NULL;
        /* VMOV/VABS/VNEG never round and never inspect a value's class, so
         * they are admissible in any rounding mode; VSQRT rounds. */
        bad = c->vfp_fpscr & (sqrt_ ? MODE_ROUNDING : MODE_EXACT);
        if (bad) return vfp_trap(pc, insn, mode_complaint(bad));

        if (dbl) {
            if (D || M) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
            rd = vd; rm = vm;
        } else {
            rd = SREG(vd, D); rm = SREG(vm, M);
        }
        if (!vfp_short_vector_shape(c->vfp_fpscr, dbl, rd, &shape, &why))
            return vfp_trap(pc, insn, why);
        if (dbl) {
            uint64_t result[4];
            for (unsigned lane = 0; lane < shape.count; lane++) {
                unsigned sr = vfp_short_vector_reg(&shape, rm, lane, true);
                uint64_t s = vfp_get_d(c, sr);
                double r;
                if (opc2 == 0u) r = top ? fabs64(u2d(s)) : u2d(s);   /* VABS/VMOV */
                else if (!top)  r = fneg64(u2d(s));                  /* VNEG      */
                else            r = f64_do(OP_SQRT, u2d(s), 0.0,
                                           c->vfp_fpscr, &exc);
                result[lane] = d2u(r);
            }
            if (exc & VFP_FZ_AMBIGUOUS) return vfp_trap(pc, insn, FZ_AMBIGUOUS_WHY);
            for (unsigned lane = 0; lane < shape.count; lane++)
                vfp_set_d(c, vfp_short_vector_reg(&shape, rd, lane, false),
                          result[lane]);
        } else {
            uint32_t result[8];
            for (unsigned lane = 0; lane < shape.count; lane++) {
                unsigned sr = vfp_short_vector_reg(&shape, rm, lane, true);
                uint32_t s = vfp_get_s(c, sr);
                float r;
                if (opc2 == 0u) r = top ? fabs32(u2f(s)) : u2f(s);
                else if (!top)  r = fneg32(u2f(s));
                else            r = f32_do(OP_SQRT, u2f(s), 0.0f,
                                           c->vfp_fpscr, &exc);
                result[lane] = f2u(r);
            }
            if (exc & VFP_FZ_AMBIGUOUS) return vfp_trap(pc, insn, FZ_AMBIGUOUS_WHY);
            for (unsigned lane = 0; lane < shape.count; lane++)
                vfp_set_s(c, vfp_short_vector_reg(&shape, rd, lane, false),
                          result[lane]);
        }
        c->vfp_fpscr |= exc;
        return ARM_OK;
    }

    /* ---- VCMP / VCMPE, against a register (4) or against +0.0 (5). ----
     * The result goes to FPSCR.NZCV, never to CPSR. VCMPE signals on any NaN;
     * plain VCMP signals only on a signalling one. Neither rounds. */
    case 4u: case 5u: {
        bool zero = (opc2 == 5u);
        unsigned rd, rm;
        int order;
        bool nan_op, snan_op;

        bad = c->vfp_fpscr & MODE_VALUES;
        if (bad) return vfp_trap(pc, insn, mode_complaint(bad));
        if (zero && (vm != 0u || M))
            return vfp_trap(pc, insn, "VCMP #0.0 with a non-zero Vm field");

        if (dbl) {
            if (D || M) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
            rd = vd; rm = vm;
        } else {
            rd = SREG(vd, D); rm = SREG(vm, M);
        }

        /* FPCompare unpacks its operands, so flush-to-zero applies: with FZ
         * set a denormal compares equal to zero rather than less than the
         * smallest normal. The signalling-NaN test reads the ORIGINAL bits,
         * which is safe because a denormal is never a NaN. */
        if (dbl) {
            uint64_t ua = vfp_get_d(c, rd);
            uint64_t ub = zero ? 0ull : vfp_get_d(c, rm);
            double a = u2d(ua), b = u2d(ub);
            if (c->vfp_fpscr & ARM_FPSCR_FZ) {
                a = fz_in64(a, &exc); b = fz_in64(b, &exc);
            }
            nan_op  = (a != a) || (b != b);
            snan_op = snan64(ua) || (!zero && snan64(ub));
            order = nan_op ? 2 : (a == b ? 0 : (a < b ? -1 : 1));
        } else {
            uint32_t ua = vfp_get_s(c, rd);
            uint32_t ub = zero ? 0u : vfp_get_s(c, rm);
            float a = u2f(ua), b = u2f(ub);
            if (c->vfp_fpscr & ARM_FPSCR_FZ) {
                a = fz_in32(a, &exc); b = fz_in32(b, &exc);
            }
            nan_op  = (a != a) || (b != b);
            snan_op = snan32(ua) || (!zero && snan32(ub));
            order = nan_op ? 2 : (a == b ? 0 : (a < b ? -1 : 1));
        }
        if (snan_op || (top && nan_op)) exc |= ARM_FPSCR_IOC;
        c->vfp_fpscr = (c->vfp_fpscr & ~ARM_FPSCR_NZCV)
                     | cmp_flags_ordered(order) | exc;
        return ARM_OK;
    }

    /* ---- VCVT between double and single. sz names the SOURCE width. ---- */
    case 7u: {
        if (!top) return vfp_trap(pc, insn, "UNDEFINED VFP data-processing opcode");
        /* Narrowing rounds; widening is always exact. */
        bad = c->vfp_fpscr & (dbl ? MODE_ROUNDING : MODE_VALUES);
        if (bad) return vfp_trap(pc, insn, mode_complaint(bad));

        if (dbl) {                                   /* VCVT.F32.F64        */
            volatile float vr;
            float r;
            double s;
            if (M) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
            s = u2d(vfp_get_d(c, vm));
            if (c->vfp_fpscr & ARM_FPSCR_FZ) s = fz_in64(s, &exc);
            {   volatile double vs = s; double x;
                host_exceptions_clear();
                x = vs; vr = (float)x;
                exc |= host_exceptions();
            }
            r = vr;
            if (c->vfp_fpscr & ARM_FPSCR_FZ) r = fz_out32(r, &exc);
            if (exc & VFP_FZ_AMBIGUOUS) return vfp_trap(pc, insn, FZ_AMBIGUOUS_WHY);
            vfp_set_s(c, SREG(vd, D), f2u(dn_out32(r, c->vfp_fpscr)));
        } else {                                     /* VCVT.F64.F32        */
            volatile double vr;
            float s;
            if (D) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
            s = u2f(vfp_get_s(c, SREG(vm, M)));
            if (c->vfp_fpscr & ARM_FPSCR_FZ) s = fz_in32(s, &exc);
            {   volatile float vs = s; float x;
                host_exceptions_clear();
                x = vs; vr = (double)x;
                exc |= host_exceptions();
            }
            /* Widening a binary32 into a binary64 can never produce a
             * denormal, so there is no output flush to consider here. */
            vfp_set_d(c, vd, d2u(dn_out64(vr, c->vfp_fpscr)));
        }
        c->vfp_fpscr |= exc;
        return ARM_OK;
    }

    /* ---- VCVT integer -> floating point. The source is always a single
     * register holding a 32-bit integer; bit 7 selects signed. ---- */
    case 8u: {
        uint32_t raw;
        /*
         * The source is an INTEGER, so there is nothing for flush-to-zero to
         * unpack and no NaN for default-NaN to replace; neither mode can reach
         * this instruction. int32 -> binary64 is exact for every input and so
         * cannot raise anything either. int32 -> binary32 can round and can be
         * inexact, so it is gated on the rounding mode and the trap enables.
         *
         * That distinction is not pedantry: dyld runs in RunFast mode with
         * FPSCR.FZ set, and its very first floating-point instruction is
         * VCVT.F64.U32 on the mach_timebase_info numerator.
         */
        bad = c->vfp_fpscr & (dbl ? MODE_EXACT : MODE_ROUNDING);
        if (bad) return vfp_trap(pc, insn, mode_complaint(bad));

        raw = vfp_get_s(c, SREG(vm, M));
        if (dbl) {
            if (D) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
            vfp_set_d(c, vd, d2u(top ? (double)(int32_t)raw : (double)raw));
        } else {
            double exact = top ? (double)(int32_t)raw : (double)raw;
            volatile float vr;
            {   volatile double vs = exact; double x;
                host_exceptions_clear();
                x = vs; vr = (float)x;
                exc |= host_exceptions();
            }
            vfp_set_s(c, SREG(vd, D), f2u(vr));
        }
        c->vfp_fpscr |= exc;
        return ARM_OK;
    }

    /* ---- VCVT / VCVTR floating point -> integer. The destination is always
     * a single register. bit 16 selects signed, bit 7 selects VCVT's
     * round-toward-zero over VCVTR's FPSCR rounding. ---- */
    case 12u: case 13u: {
        bool is_signed = (opc2 & 1u) != 0;
        bool to_zero   = top;
        double v;

        /*
         * VCVTR consults FPSCR.RMode; VCVT does not. Both are admissible in a
         * directed rounding mode because fp_to_int() rounds in software and
         * therefore implements RMode exactly -- unlike the arithmetic paths,
         * which delegate to the host FPU and still refuse. UIKit converts with
         * VCVTR under a directed mode on the way to SpringBoard's first frame,
         * so refusing here stopped the boot on a conversion the ARM1176
         * genuinely performs.
         */
        bad = c->vfp_fpscr & MODE_VALUES;
        if (bad) return vfp_trap(pc, insn, mode_complaint(bad));

        if (dbl) {
            if (M) return vfp_trap(pc, insn, "d16-d31 do not exist on VFPv2");
            v = u2d(vfp_get_d(c, vm));
            if (c->vfp_fpscr & ARM_FPSCR_FZ) v = fz_in64(v, &exc);
        } else {
            float s = u2f(vfp_get_s(c, SREG(vm, M)));
            if (c->vfp_fpscr & ARM_FPSCR_FZ) s = fz_in32(s, &exc);
            v = (double)s;                               /* widening: exact  */
        }
        /* The destination is an integer, so there is no output to flush and no
         * NaN to replace; FZ and DN stop at the input. */
        vfp_set_s(c, SREG(vd, D),
                  fp_to_int(v, is_signed, to_zero, c->vfp_fpscr, &exc));
        c->vfp_fpscr |= exc;
        return ARM_OK;
    }

    case 2u: case 3u:
        return vfp_trap(pc, insn, "VCVTB/VCVTT half-precision; VFPv3 only");
    case 10u: case 11u: case 14u: case 15u:
        return vfp_trap(pc, insn, "VCVT fixed-point; VFPv3 only");
    default:
        return vfp_trap(pc, insn, "UNDEFINED VFP data-processing opcode");
    }
}

static arm_status_t vfp_dp(arm_cpu_t *c, uint32_t pc, uint32_t insn) {
    unsigned op = (BIT(23) << 2) | (BIT(21) << 1) | BIT(20);
    if (op == 7u) {
        if (BIT(6)) return vfp_dp_other(c, pc, insn);
        return vfp_trap(pc, insn, "VMOV (immediate); VFPv3 only");
    }
    return vfp_dp_arith(c, pc, insn, op);
}

/* ============================================================ entry ====== */

/*
 * FPSCR.RMode -> the host FPU's rounding mode.
 *
 * The arithmetic operations hand their values to the host FPU, so the only
 * honest way to implement a directed rounding mode for them is to put the host
 * in that mode for the duration of the instruction. Refusing instead was
 * defensible while nothing needed it; UIKit needs it on the way to
 * SpringBoard's first frame, and it sets the mode and then runs whole
 * sequences of conversions and arithmetic under it.
 *
 * The common case is round-to-nearest, where this costs nothing: the mode is
 * only queried and set when FPSCR actually selects a directed mode.
 */
static int host_round_from_fpscr(uint32_t fpscr) {
    switch ((fpscr & ARM_FPSCR_RMODE) >> 22) {
    case 1u:  return FE_UPWARD;        /* RP */
    case 2u:  return FE_DOWNWARD;      /* RM */
    case 3u:  return FE_TOWARDZERO;    /* RZ */
    default:  return FE_TONEAREST;     /* RN */
    }
}

static arm_status_t vfp_execute_inner(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                                      const vfp_bus_t *bus);

arm_status_t vfp_execute(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                         const vfp_bus_t *bus) {
    if (!c || (c->vfp_fpscr & ARM_FPSCR_RMODE) == 0u)
        return vfp_execute_inner(c, pc, insn, bus);

    /*
     * A directed mode is in force. Adopt it for exactly this instruction and
     * put the host back afterwards, so nothing else in the emulator inherits
     * it. If the host refuses the mode, say so rather than rounding the wrong
     * way and reporting success.
     */
    int want = host_round_from_fpscr(c->vfp_fpscr);
    int saved = fegetround();
    if (want != saved && fesetround(want) != 0)
        return vfp_trap(pc, insn, "host FPU refused FPSCR.RMode");
    arm_status_t status = vfp_execute_inner(c, pc, insn, bus);
    if (want != saved) (void)fesetround(saved);
    return status;
}

static arm_status_t vfp_execute_inner(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                                      const vfp_bus_t *bus) {
    g_reason = NULL;

    /*
     * Availability, which is the lazy-enable mechanism itself and NOT an
     * unimplemented-encoding trap: returning ARM_UNDEFINED here is how the
     * guest's own handler gets to run and switch VFP on. It must stay silent —
     * it happens on every first VFP instruction of every thread.
     *
     * With FPEXC.EN clear, privileged VMRS/VMSR of FPEXC (CRn 8) and FPSID
     * (CRn 0) remain accessible; everything else is UNDEFINED.
     * _get_vfp_enabled reads FPEXC precisely when VFP is off, and _vfp_switch
     * writes FPEXC before it touches FPSCR, so this asymmetry is load-bearing.
     */
    if (!vfp_cpacr_permits(c)) return ARM_UNDEFINED;
    if (!vfp_enabled(c)) {
        /* VMRS/VMSR share one pattern; bit 20 (L) is left out of the mask. */
        bool is_sysreg = (insn & 0x0fe00f10u) == 0x0ee00a10u;
        unsigned crn = FIELD(16);
        if (!is_sysreg || (crn != 0u && crn != 8u)) return ARM_UNDEFINED;
    }

    /* VFP 32-bit register transfer: MCR/MRC on cp10/cp11. */
    if ((insn & 0x0f000e10u) == 0x0e000a10u) return vfp_xfer32(c, pc, insn);
    /* VFP data processing: the CDP form. */
    if ((insn & 0x0f000e10u) == 0x0e000a00u) return vfp_dp(c, pc, insn);
    /* VFP 64-bit register transfer: MCRR/MRRC on cp10/cp11. */
    if ((insn & 0x0fe00e00u) == 0x0c400a00u) return vfp_xfer64(c, pc, insn);
    /* VFP load/store: the LDC/STC form. */
    if ((insn & 0x0e000e00u) == 0x0c000a00u) return vfp_ldst(c, pc, insn, bus);

    /*
     * Reached only for a cp10/cp11 encoding outside all four groups — the CDP2
     * / LDC2 / MCR2 unconditional forms, which VFP does not define. Advanced
     * SIMD (0xF2/0xF3/0xF4) never arrives here; it is refused in arm_step,
     * because the ARM1176 has no NEON at all.
     */
    return vfp_trap(pc, insn, "not a VFP encoding this unit defines");
}
