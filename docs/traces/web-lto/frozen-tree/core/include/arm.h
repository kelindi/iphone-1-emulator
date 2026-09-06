/*
 * S5LBox — ARMv6 (ARM1176JZF-S) CPU core, public interface.
 *
 * The ARM1176JZF-S is the application processor inside the Samsung S5L8900
 * (original iPhone / iPhone 3G / iPod touch 1G) that iPhone OS 1–3 ran on.
 * This core is written as portable C11 with zero platform dependencies so it
 * builds and unit-tests on Windows/Linux/macOS and drops unchanged into the
 * iOS app. Memory is reached through host callbacks (see arm_bus_t).
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_ARM_H
#define S5LBOX_ARM_H

#include <stdint.h>
#include <stdbool.h>

/* CPSR condition-flag bit positions. */
#define ARM_CPSR_N (1u << 31) /* Negative */
#define ARM_CPSR_Z (1u << 30) /* Zero     */
#define ARM_CPSR_C (1u << 29) /* Carry    */
#define ARM_CPSR_V (1u << 28) /* Overflow */
/* Sticky saturation. Set by the saturating arithmetic, never cleared by it —
 * software clears it with MSR when it wants to test a span of operations. */
#define ARM_CPSR_Q (1u << 27)

/* CPSR control bits. */
/* A: imprecise (asynchronous) data-abort disable, new in ARMv6. Set on entry
 * to Reset, Prefetch Abort, Data Abort, IRQ and FIQ — see take_exception. We
 * model no asynchronous abort source, so nothing is actually masked by it; it
 * exists so the CPSR and every SPSR the guest saves hold the value hardware
 * would have put there. */
#define ARM_CPSR_A (1u << 8)  /* async abort disable */
/*
 * E: the data endianness of loads and stores, new in ARMv6 (SETEND writes it).
 * This machine is little-endian throughout and SETEND BE traps, so E is never
 * set by anything we execute — but MSR CPSR_x can set it, and the architecture
 * makes exception entry OVERWRITE it from SCTLR.EE rather than leave it alone.
 * take_exception does that, so a guest that set E cannot carry a big-endian
 * data path into a handler, and the SPSR it stacks records the truth.
 */
#define ARM_CPSR_E (1u << 9)  /* data endianness: 0 little, 1 big */
#define ARM_CPSR_I (1u << 7)  /* IRQ disable  */
#define ARM_CPSR_F (1u << 6)  /* FIQ disable  */
#define ARM_CPSR_T (1u << 5)  /* Thumb state  */
#define ARM_CPSR_MODE_MASK 0x1fu

/* CP15 c1 system control register (SCTLR) bits we act on. */
#define ARM_SCTLR_M (1u << 0)   /* MMU enable          */
#define ARM_SCTLR_A (1u << 1)   /* alignment check     */
#define ARM_SCTLR_C (1u << 2)   /* data cache enable   */
#define ARM_SCTLR_S (1u << 8)   /* legacy system protection */
#define ARM_SCTLR_R (1u << 9)   /* legacy ROM protection    */
#define ARM_SCTLR_I (1u << 12)  /* instruction cache   */
#define ARM_SCTLR_V (1u << 13)  /* high exception vectors @ 0xFFFF0000 */
#define ARM_SCTLR_U (1u << 22)  /* ARMv6 unaligned half/word support */
#define ARM_SCTLR_EE (1u << 25) /* CPSR.E value taken on exception entry */
#define ARM_SCTLR_FA (1u << 29) /* force extended-descriptor access flag */
/*
 * XP: extended page tables. Clear, the MMU reads the ARMv5-compatible
 * descriptor layout, where a small page carries four sets of subpage AP bits
 * and there is no execute-never attribute anywhere. Set, descriptors use the
 * ARMv6 layout this core already decodes (single AP + APX) and the XN bit
 * exists: bit 4 of a section/supersection, bit 15 of a large page, bit 0 of a
 * small page. XN is therefore only meaningful with XP set, and the fetch path
 * checks it only then. XNU sets XP as part of the 0xc0380d it ORs into SCTLR
 * at __start+0x16c (0xc00691ac).
 */
#define ARM_SCTLR_XP (1u << 23) /* extended (ARMv6) page tables; enables XN */

/* Main ID register value reported for the ARM1176JZF-S in the S5L8900. */
#define ARM1176_MIDR       0x410fb767u
#define ARM1176_CACHE_TYPE 0x1d152152u
/* VFP11 identity register as reported by the ARM1176JZF-S. */
#define ARM1176_FPSID      0x410120b4u

/*
 * FPEXC, the VFP exception register. Only EN matters here: it is the switch
 * XNU flips to enable VFP for a thread, and with it clear every VFP
 * instruction except an access to FPEXC or FPSID is UNDEFINED. That is not a
 * quirk to work around — it is the mechanism the kernel relies on to enable
 * VFP lazily, one thread at a time. See vfp_lazy_enable_trap in arm_interp.c.
 */
#define ARM_FPEXC_EX (1u << 31)  /* exceptional state                        */
#define ARM_FPEXC_EN (1u << 30)  /* VFP enabled for the current thread       */

/*
 * CPACR, the coprocessor access control register, fields for CP10 and CP11 —
 * the two halves of the VFP/Advanced SIMD unit. Two bits each:
 *   00 access denied (any access is UNDEFINED)
 *   01 privileged access only (User-mode access is UNDEFINED)
 *   10 reserved
 *   11 full access
 * XNU's _init_vfp (0xc0069938) does "CPACR |= 0xf << 20", granting full
 * access to both, and then gates per thread with FPEXC.EN alone.
 */
#define ARM_CPACR_CP10_SHIFT 20
#define ARM_CPACR_CP11_SHIFT 22

/*
 * The ARMv6 CPUID feature identification block: CP15 c0 with CRm == 1
 * (processor / memory-model features) and CRm == 2 (instruction set
 * attributes). These are read-only and, on the ARM1176JZF-S, constant —
 * the values below are the part's, from ARM DDI 0301H section 3.2.
 *
 * They are not optional decoration. MIDR[19:16] on this core reads 0xF,
 * which by the ARM ARM means "architecture version is described by the
 * CPUID scheme, not by this field" — so any reader that wants the actual
 * architecture MUST come here. XNU does exactly that: do_cpuid() sees the
 * 0xF, reads ID_ISAR1, checks the Jazelle field, and only then rewrites
 * its cached MIDR to say ARMv6. Without ID_ISAR1 the kernel concludes the
 * CPU has no known architecture and reports CPU_SUBTYPE_ARM_ALL, which
 * makes grade_binary() reject every ARMv6 executable on the disk.
 */
#define ARM1176_ID_PFR0    0x00000111u  /* ARM + Thumb + Jazelle state     */
#define ARM1176_ID_PFR1    0x00000001u  /* v6 programmer model; no Security Extensions */
/*
 * ID_DFR0 is the one deliberate zero. The real part answers 0x00000033 —
 * "coprocessor-accessed debug, ARMv6.1" — but we do not implement the CP14
 * debug unit; DBGDIDR reads as zero here. XNU's do_debugid() takes a
 * non-zero ID_DFR0 as licence to publish a breakpoint/watchpoint count
 * derived from DBGDIDR, so claiming 0x33 would advertise debug hardware
 * that does not exist. Zero is the truthful answer for this machine, and
 * the architecture defines it to mean "no debug support".
 */
#define ARM1176_ID_DFR0    0x00000000u
#define ARM1176_ID_AFR0    0x00000000u  /* no implementation-defined features */
#define ARM1176_ID_MMFR0   0x01130003u
#define ARM1176_ID_MMFR1   0x10030302u
#define ARM1176_ID_MMFR2   0x01222100u
#define ARM1176_ID_MMFR3   0x00000000u
#define ARM1176_ID_ISAR0   0x00140011u
#define ARM1176_ID_ISAR1   0x12002111u  /* [15:12] = 2: Jazelle BXJ + J bit */
#define ARM1176_ID_ISAR2   0x11231121u
#define ARM1176_ID_ISAR3   0x01102131u
#define ARM1176_ID_ISAR4   0x00000141u
#define ARM1176_ID_ISAR5   0x00000000u

/* ARMv6 fault status codes (FSR[3:0]); FSR[7:4] carries the domain. */
#define ARM_FSR_ALIGNMENT           0x1u
#define ARM_FSR_SECTION_ACCESS_FLAG 0x3u
#define ARM_FSR_SECTION_TRANSLATION 0x5u
#define ARM_FSR_PAGE_ACCESS_FLAG    0x6u
#define ARM_FSR_PAGE_TRANSLATION    0x7u
#define ARM_FSR_SECTION_DOMAIN      0x9u
#define ARM_FSR_PAGE_DOMAIN         0xbu
#define ARM_FSR_SECTION_PERMISSION  0xdu
#define ARM_FSR_PAGE_PERMISSION     0xfu

/* Exception vector offsets (added to 0x0, or 0xFFFF0000 when SCTLR.V is set). */
#define ARM_VEC_RESET      0x00u
#define ARM_VEC_UNDEFINED  0x04u
#define ARM_VEC_SWI        0x08u
#define ARM_VEC_PREFETCH   0x0cu
#define ARM_VEC_DATA_ABORT 0x10u
#define ARM_VEC_IRQ        0x18u
#define ARM_VEC_FIQ        0x1cu

/* Processor modes (CPSR[4:0]). Only the ones we currently model are named. */
#define ARM_MODE_USR 0x10
#define ARM_MODE_FIQ 0x11
#define ARM_MODE_IRQ 0x12
#define ARM_MODE_SVC 0x13
#define ARM_MODE_ABT 0x17
#define ARM_MODE_UND 0x1b
#define ARM_MODE_SYS 0x1f

/* Result of stepping the core. */
typedef enum {
    ARM_OK = 0,          /* one instruction retired normally           */
    ARM_UNDEFINED,       /* unsupported encoding: stop for diagnosis    */
    ARM_HALT,            /* core requested halt (e.g. debug trap)       */
    ARM_GUEST_UNDEFINED  /* architected Undefined exception: vector it  */
} arm_status_t;

/*
 * Result from the optional privileged-SVC host hook below. HANDLED consumes
 * the instruction and retires sequentially. REDIRECTED also consumes it, but
 * retains the callback's r15 and CPSR.T as the next fetch target and ISA.
 * UNHANDLED and values not named by this enum take the ordinary guest SVC
 * exception. ERROR stops arm_step with ARM_HALT after rolling back CPU
 * changes, so a failed host service cannot be mistaken for a guest syscall or
 * let execution continue.
 */
typedef enum {
    ARM_SVC_UNHANDLED  = 0,
    ARM_SVC_HANDLED    = 1,
    ARM_SVC_REDIRECTED = 2,
    ARM_SVC_ERROR      = -1
} arm_svc_result_t;

struct arm_cpu;

typedef arm_svc_result_t (*arm_privileged_svc_handler_t)(void *ctx,
                                                          struct arm_cpu *cpu,
                                                          uint32_t pc,
                                                          uint32_t encoding);

/*
 * The system bus. The CPU is agnostic about what lives behind these; the test
 * harness supplies flat RAM, the machine layer supplies the S5L8900 memory map.
 * All accesses are little-endian, matching the guest.
 */
typedef struct arm_bus {
    void    *ctx;
    uint32_t (*read32)(void *ctx, uint32_t addr);
    uint16_t (*read16)(void *ctx, uint32_t addr);
    uint8_t  (*read8 )(void *ctx, uint32_t addr);
    void     (*write32)(void *ctx, uint32_t addr, uint32_t val);
    void     (*write16)(void *ctx, uint32_t addr, uint16_t val);
    void     (*write8 )(void *ctx, uint32_t addr, uint8_t  val);

    /*
     * OPTIONAL. A host pointer for [pa, pa+len) when that whole range is plain
     * writable RAM, and NULL for anything else -- MMIO, NOR, a stub window, or
     * a range that straddles the end of RAM.
     *
     * It exists so a translation can be turned into a pointer once and reused,
     * instead of paying an indirect call through read32() plus a bounds check
     * plus a runtime-variable memcpy on every access. NULL is always a correct
     * answer and simply keeps the caller on the slow path; the flat-RAM test
     * harnesses leave this unset.
     *
     * The pointer must stay valid until the next reset or snapshot restore.
     * Nothing derived from it may be snapshotted -- a host address means
     * nothing in another process -- which is why the fetch cache below is
     * zeroed rather than serialised.
     *
     * READS MAY USE THIS. WRITES MAY NOT, and the reason is not obvious:
     * a frontend can interpose on this bus, and bootkernel does so
     * unconditionally. Its read hook ignores RAM -- `if (!is_ram(...))` -- so
     * bypassing it for a plain-RAM read observes nothing it wanted. Its WRITE
     * hook does not: it carries the live scanout observer, which exists
     * precisely to watch writes landing in the framebuffer, and the
     * framebuffer is RAM. A write that went straight to the host pointer
     * would be invisible to it, and the counter would under-report by exactly
     * the traffic it was built to measure while still printing a number.
     *
     * So a store fast path needs the frontend's consent, not just a pointer.
     * Anything adding one -- a JIT's memory path will want to -- has to give
     * the interposer a way to see it, or ask whether one is installed.
     */
    uint8_t *(*host_ram)(void *ctx, uint32_t pa, uint32_t len);

    /*
     * OPTIONAL, AND DELIBERATELY SEPARATE FROM host_ram. Returning a pointer
     * here is the frontend's explicit promise that CPU stores may update that
     * plain-RAM range through the pointer without calling write8/16/32.
     *
     * NULL is the safe default. A bus write interposer MUST leave this NULL or
     * clear it before guest execution; otherwise direct stores would bypass
     * the very observer the interposer installed. The machine bus can opt in
     * because its RAM write callbacks only perform the same little-endian host
     * write. bootkernel cannot opt in because its write hook observes live
     * framebuffer traffic.
     *
     * The lifetime and range contract is identical to host_ram. Keeping a
     * second callback instead of a boolean makes consent inseparable from the
     * pointer source and leaves every existing frontend fail-closed.
     */
    uint8_t *(*host_ram_write)(void *ctx, uint32_t pa, uint32_t len);

    /*
     * Optional platform hook for the ARM1176 CP15 Wait For Interrupt
     * operation.  A system model can synchronously advance its autonomous
     * devices to the first interrupt edge and return true.  Returning false
     * means that no modeled wake source can currently make progress; the core
     * then retains the historical no-op fallback instead of hanging its host.
     *
     * The callback is deliberately part of the host bus, not arm_cpu_t: WFI
     * has no architecturally visible state once it completes, and keeping the
     * wait synchronous means snapshots need no hidden "halfway through WFI"
     * bit.  The WFI itself still retires exactly once.
     */
    bool     (*wait_for_interrupt)(void *ctx);

    /*
     * Optional host-service seam for an SVC executed from a privileged mode.
     * User-mode SVCs never reach this callback. `pc` is the exact address of
     * the current instruction and `encoding` is the raw A32 word or the
     * zero-extended Thumb halfword. The callback receives the separate
     * privileged_svc_ctx and the CPU in its pre-exception state, including
     * CPSR mode/T and all registers.
     *
     * ARM_SVC_HANDLED commits CPU-register changes and retires sequentially
     * past the SVC. The core writes r15=pc+4 for A32 or r15=pc+2 for Thumb
     * afterward, so a callback's r15 write is deliberately not retained.
     * ARM_SVC_REDIRECTED commits the same CPU state, but the callback's r15
     * and CPSR.T select the next fetch address and instruction set instead of
     * the sequential address.
     * ARM_SVC_UNHANDLED and unknown values roll the CPU back and follow the
     * normal guest exception path. ARM_SVC_ERROR rolls back and makes
     * arm_step return ARM_HALT without entering the guest exception handler
     * or retiring the SVC; PC and the retired-instruction count stay fixed.
     *
     * Validate every argument, range, and operation before causing platform
     * side effects through privileged_svc_ctx. CPU state is transactional,
     * but the core cannot undo host I/O. Both fields are host-owned runtime
     * configuration and are deliberately excluded from snapshots.
     */
    arm_privileged_svc_handler_t privileged_svc_handler;
    void                         *privileged_svc_ctx;
} arm_bus_t;

/*
 * Register banks. ARM banks r13/r14 per privileged mode, and additionally
 * banks r8–r12 for FIQ. USR and SYS share one bank and have no SPSR.
 */
typedef enum {
    ARM_BANK_USR = 0, ARM_BANK_FIQ, ARM_BANK_IRQ,
    ARM_BANK_SVC, ARM_BANK_ABT, ARM_BANK_UND, ARM_BANK_COUNT
} arm_bank_t;

/*
 * CP15, the system control coprocessor. The kernel programs the MMU, caches,
 * and vector base through here, so these values steer real execution.
 */
typedef struct arm_cp15 {
    uint32_t sctlr;       /* c1,c0,0  system control            */
    uint32_t actlr;       /* c1,c0,1  auxiliary control         */
    uint32_t cpacr;       /* c1,c0,2  coprocessor access        */
    uint32_t ttbr0;       /* c2,c0,0  translation table base 0  */
    uint32_t ttbr1;       /* c2,c0,1  translation table base 1  */
    uint32_t ttbcr;       /* c2,c0,2  translation table control */
    uint32_t dacr;        /* c3,c0,0  domain access control     */
    uint32_t dfsr, ifsr;  /* c5       fault status              */
    uint32_t dfar, ifar;  /* c6       fault address             */
    uint32_t fcse_pid;    /* c13,c0,0 */
    uint32_t context_id;  /* c13,c0,1 */
    /* Software thread-ID registers. The kernel keeps per-CPU data in TPIDRPRW,
     * so these must actually store rather than read back as zero. */
    uint32_t tpidrurw;    /* c13,c0,2 user read/write */
    uint32_t tpidruro;    /* c13,c0,3 user read-only  */
    uint32_t tpidrprw;    /* c13,c0,4 privileged only */
} arm_cp15_t;

/*
 * Which architecture variant this core implements.
 *
 * The emulator is built around one machine at a time, but the instruction set
 * is not a property of the machine's peripherals -- it is a property of the
 * core, and a second machine profile needs a second answer. Keeping it as an
 * explicit field rather than a compile-time switch means both variants can be
 * exercised by the same test binary, which is the only way an ARMv7-only
 * encoding can be proven to still be REFUSED on the ARM1176.
 *
 * ARM_ARCH_V6_ARM1176 is deliberately zero, so every zero-initialised
 * arm_cpu_t keeps exactly the behaviour it had before this field existed.
 */
typedef enum {
    ARM_ARCH_V6_ARM1176 = 0,  /* S5L8900 / iPhone OS 3 -- the current target */
    ARM_ARCH_V7_SWIFT   = 1   /* S5L8950X / iPhone 5, ARMv7s -- roadmap P2   */
} arm_arch_t;

/*
 * Direct-mapped, power of two so the index is a mask rather than a modulo.
 *
 * KEYED AT 1 KB, NOT 4 KB, and that is not a tuning choice. ARMv6 legacy small
 * pages carry FOUR sets of AP bits, one per 1 KB subpage, so two addresses in
 * the same 4 KB page can have different permissions. A 4 KB-keyed cache
 * answers one subpage's verdict for another -- which is exactly what
 * test_legacy_subpage_permissions caught on the first attempt, on the COW and
 * user-denied paths where it matters most. 1 KB is the finest granularity the
 * architecture has, so it is the only key that cannot be wrong.
 */
#define ARM_TLB_ENTRIES 4096u

/*
 * Data-read block cache entries: 64 x 16 bytes = 1 KB, half per privilege.
 * Small on purpose -- it wants to stay in L1 alongside the guest's working
 * set, and unlike the 4096-entry TLB it caches only plain RAM.
 */
#define ARM_DREAD_ENTRIES 64u

typedef struct arm_cpu {
    uint32_t r[16];      /* r0–r15; r15 is PC (address of current instruction) */
    arm_arch_t arch;     /* zero => ARM1176, so existing callers are unchanged */
    uint32_t cpsr;
    arm_cp15_t cp15;
    uint32_t spsr[ARM_BANK_COUNT];     /* saved CPSR per privileged bank        */
    uint32_t bank_r13[ARM_BANK_COUNT]; /* banked stack pointers                 */
    uint32_t bank_r14[ARM_BANK_COUNT]; /* banked link registers                 */
    uint32_t fiq_r8_12[5];             /* FIQ's private r8–r12                  */
    uint32_t usr_r8_12[5];             /* user r8–r12 parked while in FIQ mode  */
    uint64_t cycles;     /* retired-instruction counter (1 insn == 1 tick for now) */
    const arm_bus_t *bus;

    /* A translation fault raised mid-instruction; arm_step turns this into a
     * data abort once the instruction finishes. */
    bool     abort_pending;
    uint32_t abort_fsr;
    uint32_t abort_far;

    /* Interrupt request lines, driven by the interrupt controller. Sampled at
     * the top of each arm_step and honoured unless masked by CPSR I/F. */
    bool irq_line;
    bool fiq_line;

    /* ARMv6 exclusive monitor for LDREX/STREX. Every XNU atomic and every
     * spinlock goes through these, so they are load-bearing rather than
     * optional. One CPU means a single address tag is sufficient. */
    bool     excl_valid;
    uint32_t excl_addr;

    /* VFP11 system registers. The kernel disables VFP and enables it lazily
     * per thread, so the context-switch path reads and writes FPEXC on every
     * switch. FPSID is a constant (ARM1176_FPSID) and so is not stored. */
    uint32_t vfp_fpexc;
    uint32_t vfp_fpscr;
    /*
     * The VFP register file: s0-s31 as raw bit patterns, in register-number
     * order. VFPv2 on the ARM1176 has 16 double-precision registers ALIASED
     * onto these, not a separate bank: dN is the pair (vfp_s[2N], vfp_s[2N+1])
     * with the LOW-order word first. Keeping one array and deriving dN from it
     * is what makes the aliasing impossible to get wrong — there is no second
     * copy that could drift. There is no d16-d31 on this part; see vfp.c.
     */
    uint32_t vfp_s[32];

    /*
     * THE TRANSLATION CACHE, and why the model went without one for so long.
     *
     * arm_mmu_translate() walks the guest's page tables on EVERY fetch and
     * EVERY data access, which costs two bus reads each because there was
     * nothing remembering the last answer. insnbench measures the price
     * directly: 9.15 Minsn/s with the MMU off against 4.32 with 4 KB pages,
     * so a guest kernel -- which runs with the MMU on permanently -- pays
     * better than 2x for address translation alone.
     *
     * This is a CACHE and nothing else. A hit returns exactly what a walk
     * would have returned, including the fault code, so no behaviour depends
     * on whether an entry was present. That is the property that makes it
     * safe to flush at any time and to restore a snapshot with it empty.
     *
     * ARCHITECTURALLY THIS MAKES THE MODEL MORE FAITHFUL, NOT LESS. Real
     * ARMv6 has a TLB, so the guest is already obliged to issue CP15 c8
     * maintenance after editing a descriptor -- and c8 was a documented no-op
     * here precisely because there was no TLB to maintain. A guest that edits
     * tables without invalidating was always broken on hardware; now it is
     * broken here too, which is the correct behaviour to model.
     *
     * Keyed on the 4 KB page AND on the access kind and privilege, because
     * permission depends on both and caching a translation without them would
     * answer a fetch with a load's verdict. Sections and supersections simply
     * occupy several entries; correctness does not depend on page size.
     */
    struct {
        uint32_t gen;    /* the flush generation this entry was filled in    */
        uint32_t tag;    /* (va >> 10) << 3 | acc << 1 | priv                */
        uint32_t pa;     /* 1 KB-aligned base, already masked                */
        uint32_t fsr;    /* what the walk returned, faults included          */
    } tlb[ARM_TLB_ENTRIES];
    uint32_t tlb_gen;
    /*
     * THE INSTRUCTION-FETCH BLOCK CACHE.
     *
     * Every retired instruction pays one fetch, and a fetch inside a 1 KB
     * block repeats work it already did up to 1024 times: the SCTLR test, the
     * generation test, six stamp compares, the hash, the tag compare, and then
     * -- even on a hit -- an indirect call through bus->read32 into a bounds
     * check and a runtime-variable memcpy. The TLB caches a PHYSICAL address,
     * so a hit does not avoid any of the second half.
     *
     * This caches the host pointer for the block the last fetch resolved to,
     * so a fetch that stays inside it is a compare and a load. The block is
     * 1 KB, matching the TLB's key, because ARMv6 legacy small pages carry
     * four independent sets of AP bits -- one per 1 KB subpage -- and a 4 KB
     * cache would answer one subpage's fetch with another's permission.
     *
     * Instructions are aligned, so a fetch never straddles the block: ARM at
     * offset 1020 spans to 1024, Thumb at 1022 to 1024.
     *
     * Coherency is free rather than assumed. The pointer aliases the same host
     * memory bus writes land in, so guest stores -- including a kext being
     * paged in over this very block -- are visible with no invalidation. Only
     * plain RAM is ever cached, so MMIO fetches keep the slow path.
     *
     * NOT SNAPSHOTTED. A host address is meaningless in another process, so
     * snap_cpu writes zeros and the cache refills on the first fetch.
     */
    uint8_t *fetch_host;   /* host pointer to fetch_blk's first byte, or NULL */
    uint32_t fetch_blk;    /* the 1 KB-aligned VA it covers                   */
    uint32_t fetch_gen;    /* the tlb_gen it was resolved under               */
    bool     fetch_priv;   /* and the privilege, which changes permission     */
    /*
     * THE DATA-READ BLOCK CACHE — the same trick as the fetch cache above,
     * applied to the other half of §6.1 of docs/dynarec.md.
     *
     * A load that HITS the TLB still pays an indirect call through
     * bus->read32, a chain of range comparisons, and a runtime-variable
     * memcpy, because the TLB caches a PHYSICAL address and none of that
     * second half depends on translation. This caches the host pointer
     * instead, so a load into an already-resolved block is a compare and a
     * load.
     *
     * READS ONLY, and that restriction is load-bearing rather than laziness.
     * See host_ram's contract: bootkernel's read hook ignores RAM, so
     * bypassing it observes nothing it wanted; its WRITE hook carries the live
     * scanout observer and the framebuffer is RAM, so a store fast path would
     * silently under-report exactly the traffic it exists to measure. A store
     * path needs the frontend's consent, which is a separate change.
     *
     * DIRECT-MAPPED, AND SPLIT BY PRIVILEGE THE WAY THE TAG IS. The TLB above
     * was once indexed on the page alone while its tag also carried the access
     * kind, so four tags competed for one slot and 11.1 M of 11.2 M lookups
     * missed. Privilege is folded into the index here for that reason, not for
     * symmetry: the tag distinguishes it, so the index must too.
     *
     * Unlike a fetch, a data access is not guaranteed aligned (SCTLR.U=1
     * permits unaligned ordinary accesses), so it CAN straddle the end of a
     * block. The fast path is therefore taken only when the access lies wholly
     * inside one, which also keeps it inside the range host_ram vouched for --
     * the last block of RAM would otherwise be read four bytes past its end,
     * where bus_read would have fallen through to the device decode instead.
     *
     * NOT SNAPSHOTTED, for the same reason as the fetch cache: these are host
     * addresses. Cleared on read, on reset, and on the tlb_gen wrap.
     */
    struct {
        uint8_t *host;     /* host pointer to the block's first byte, or NULL */
        uint32_t tag;      /* 1 KB-aligned VA | privilege in bit 0            */
        uint32_t gen;      /* the tlb_gen it was resolved under               */
    } dread[ARM_DREAD_ENTRIES];
    /*
     * THE DATA-WRITE BLOCK CACHE. Its mapping key and 1 KB range are identical
     * to dread, but an entry can be filled only through host_ram_write. A hit
     * therefore proves both MMU write permission and frontend consent to skip
     * the write callbacks. Misses stay on the architectural bus path.
     *
     * Like dread this is derived host state: generation-tagged, cleared on
     * reset/restore/wrap, and never serialised.
     */
    struct {
        uint8_t *host;
        uint32_t tag;
        uint32_t gen;
    } dwrite[ARM_DREAD_ENTRIES];
    /*
     * The registers a cached translation is only valid under, kept so the
     * cache can notice them changing BY ANY ROUTE rather than only through
     * MCR. The CP15 write path flushes explicitly, but three things mutate
     * these directly: snapshot restore, the memory-disk bridges, and every
     * MMU test in core/tests/test_arm.c -- 54 translations that set SCTLR.S,
     * SCTLR.FA or DACR on the struct and expect the verdict to follow. Those
     * tests are right to: they are testing translation, not TLB discipline.
     *
     * Six compares per translation is nothing against the two bus reads of a
     * walk, and it makes a stale answer structurally impossible instead of
     * dependent on every future writer remembering to invalidate.
     */
    struct {
        uint32_t sctlr, ttbr0, ttbr1, ttbcr, dacr, context_id;
    } tlb_stamp;
    uint64_t tlb_hits, tlb_misses, tlb_flushes;
    /*
     * Data-read block cache accounting. A pure cache passes a bit-identical
     * differential run whether it works or does nothing at all, so without
     * these a dead cache and a live one are indistinguishable -- which has
     * already happened once in this tree, when a fix and its absence produced
     * identical counters and neither could be told from the other.
     *
     * A miss counts every reason for taking the slow path, including an access
     * that straddles the block and a page that is not RAM, because those are
     * slow-path traffic too and hiding them would flatter the hit rate.
     */
    uint64_t dread_hits, dread_misses;
    uint64_t dwrite_hits, dwrite_misses;
} arm_cpu_t;

/*
 * What an access is for. This is a three-way distinction, not a bool, because
 * the two things the descriptor is checked against are independent: AP/APX
 * decide read versus write, and XN decides fetch versus everything else. A
 * fetch is not "a read" — a page can be readable and still refuse to execute —
 * so a caller that cannot say "this is a fetch" can never raise the fault.
 * The numbering keeps READ/WRITE where the old bool had them.
 */
typedef enum {
    ARM_ACCESS_READ  = 0,
    ARM_ACCESS_WRITE = 1,
    ARM_ACCESS_FETCH = 2
} arm_access_t;

/*
 * Translate a virtual address. Returns 0 on success (writing the physical
 * address to *pa) or a non-zero ARMv6 fault status register value.
 * With the MMU disabled (SCTLR.M clear) translation is the identity map.
 *
 * Only ARM_ACCESS_WRITE sets FSR.WnR, and only ARM_ACCESS_FETCH is checked
 * against XN. Translation covers one 4 KB page: a caller whose access straddles
 * a page boundary must split it and translate each piece (see arm_interp.c).
 */
uint32_t arm_mmu_translate(arm_cpu_t *cpu, uint32_t va, arm_access_t acc,
                           bool priv, uint32_t *pa);

/* Rebuild the 1 KiB instruction-fetch host pointer without walking page
 * tables or touching the bus. With the MMU enabled this succeeds only from an
 * exact, current, non-faulting FETCH entry already present in the software
 * TLB; with it disabled the mapping is the architectural identity map. The
 * complete physical block must be exposed by host_ram. A successful MMU-on
 * reuse accounts for the one TLB hit that the displaced arm_step() fetch
 * would have recorded. Every refusal leaves the fetch cache and counters
 * untouched, so the caller can fall back to arm_step() for faults or MMIO. */
bool arm_fetch_cache_try_refill(arm_cpu_t *cpu, uint32_t va, bool priv);

/*
 * Drop every cached translation. Call this wherever the guest does something
 * that could change what a walk would return: CP15 c8 maintenance (which is
 * what the architecture requires of it), a TTBR/TTBCR/DACR/SCTLR write, or a
 * context-ID change. Flushing more than strictly necessary is always correct;
 * flushing less is a stale mapping that surfaces days later as a wild store.
 */
void     arm_mmu_tlb_flush(arm_cpu_t *cpu);

/* Which bank a CPSR mode value selects (USR and SYS share ARM_BANK_USR). */
arm_bank_t arm_bank_of_mode(uint32_t mode);

/* True only for processor modes implemented by this ARM1176 core. */
bool arm_mode_is_valid(uint32_t mode);

/* Switch processor mode, swapping the banked registers in and out. */
void arm_set_mode(arm_cpu_t *cpu, uint32_t mode);

/* Put the core into a defined post-reset state (SVC mode, IRQ/FIQ masked). */
void arm_reset(arm_cpu_t *cpu, const arm_bus_t *bus);

/* Install or clear the optional privileged-SVC host-service callback. */
void arm_bus_set_privileged_svc_handler(arm_bus_t *bus,
                                        arm_privileged_svc_handler_t handler,
                                        void *handler_ctx);

/* Fetch, decode, and execute exactly one instruction. */
arm_status_t arm_step(arm_cpu_t *cpu);

/* Evaluate an ARM 4-bit condition field against the current flags. */
bool arm_cond_passed(const arm_cpu_t *cpu, uint32_t cond);

#endif /* S5LBOX_ARM_H */
