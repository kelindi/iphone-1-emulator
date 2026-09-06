/*
 * S5LBox — ARMv6 interpreter.
 *
 * Scope (M1, in progress): the ARM instruction set (Thumb comes later).
 * Implemented: condition evaluation, the barrel shifter with carry-out,
 * the full data-processing group (AND..MVN) in immediate/register forms with
 * flag setting, branch/branch-with-link, single-data-transfer (LDR/STR, byte
 * and word, pre/post-indexed, writeback), and 32-bit multiply (MUL/MLA).
 *
 * Correctness is the priority for this milestone; the dynarec (see docs) will
 * later reuse this interpreter as its semantic oracle. Everything here is exact
 * ARMv6 behaviour, including r15-reads-as-PC+8 pipeline semantics.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "arm.h"
#include <string.h>
#include "vfp.h"

#include <stddef.h>

/* ------------------------------------------------------------------ helpers */

#if defined(_MSC_VER)
#define ARM_INTERP_NOINLINE __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
#define ARM_INTERP_NOINLINE __attribute__((noinline))
#else
#define ARM_INTERP_NOINLINE
#endif

static inline void set_flag(arm_cpu_t *c, uint32_t bit, bool on) {
    if (on) c->cpsr |= bit; else c->cpsr &= ~bit;
}
static inline bool get_flag(const arm_cpu_t *c, uint32_t bit) {
    return (c->cpsr & bit) != 0;
}

/* Defined below, but needed by the CP15 helper above it. */
static inline uint32_t reg_read(const arm_cpu_t *c, uint32_t pc, unsigned n);

static inline bool cpu_is_priv(const arm_cpu_t *c) {
    return (c->cpsr & ARM_CPSR_MODE_MASK) != ARM_MODE_USR;
}

/*
 * Give an explicitly opted-in platform one chance to consume a privileged
 * SVC as a host service. The architectural CPU state is transactional:
 * HANDLED and REDIRECTED commit callback changes; every other result discards
 * them. ERROR remains distinct so arm_step can stop instead of misreading
 * failed host I/O as an ordinary guest SVC. Platform state reached through
 * the callback context is outside the CPU's control and is covered by the
 * contract in arm.h.
 */
/* arm_cpu_t deliberately carries large translation/data caches.  The saved
 * rollback image below is therefore about 68 KiB in the shipping arm64 build.
 * Apple Clang LTO used to inline this helper into arm_step(), making every
 * ordinary guest instruction call ___chkstk_darwin and reserve that frame even
 * though only an actual privileged SVC can reach the snapshot.  Keep the rare
 * transactional path out of line; its architectural behavior is unchanged. */
static ARM_INTERP_NOINLINE arm_svc_result_t
privileged_svc_result(arm_cpu_t *c, uint32_t pc, uint32_t encoding) {
    if (!cpu_is_priv(c) || c->bus->privileged_svc_handler == NULL)
        return ARM_SVC_UNHANDLED;

    arm_cpu_t saved = *c;
    arm_svc_result_t result =
        c->bus->privileged_svc_handler(c->bus->privileged_svc_ctx,
                                       c, pc, encoding);
    if (result == ARM_SVC_HANDLED || result == ARM_SVC_REDIRECTED)
        return result;

    *c = saved;
    if (result == ARM_SVC_ERROR) {
        /* arm_step counted the SVC before decode, but a failed host service
         * did not retire and the machine will not tick devices after HALT.
         * Unsigned decrement deliberately reverses the increment even when
         * the pre-step count was UINT64_MAX and that increment wrapped to 0. */
        c->cycles--;
        return ARM_SVC_ERROR;
    }
    return ARM_SVC_UNHANDLED;
}

#undef ARM_INTERP_NOINLINE

/*
 * Data-side memory accessors. Every guest load/store goes through translation;
 * a fault is latched on the CPU and converted into a data abort by arm_step
 * once the instruction finishes.
 */
static void note_abort(arm_cpu_t *c, uint32_t fsr, uint32_t va) {
    if (c->abort_pending) return;          /* keep the first fault */
    c->abort_pending = true;
    c->abort_fsr = fsr;
    c->abort_far = va;
}

static void note_alignment_abort(arm_cpu_t *c, uint32_t va, bool write) {
    note_abort(c, ARM_FSR_ALIGNMENT | (write ? (1u << 11) : 0u), va);
}

static inline uint32_t legacy_rotate_word(uint32_t v, unsigned byte_offset) {
    unsigned n = (byte_offset & 3u) * 8u;
    return n ? (v >> n) | (v << (32u - n)) : v;
}

/* In the U=0/A=0 legacy model an odd ordinary halfword access is
 * architecturally UNPREDICTABLE (ARM1176 TRM Table 4-3). The memory interface
 * might align it down internally, but software is not entitled to that value. */
static inline bool legacy_halfword_unpredictable(const arm_cpu_t *c,
                                                  uint32_t va) {
    return (va & 1u) != 0u &&
           (c->cp15.sctlr & (ARM_SCTLR_U | ARM_SCTLR_A)) == 0u;
}
/*
 * Page-crossing unaligned accesses.
 *
 * XNU clears SCTLR.A and sets SCTLR.U (see the SCTLR write at __start+0x16c),
 * so unaligned LDR/STR/LDRH/STRH are legal and the kernel and libSystem use
 * them freely. Translation, however, maps one page: an access whose bytes
 * straddle a 4 KB boundary lives in two pages that are almost never physically
 * adjacent, and it can be permitted in the first and faulting in the second.
 * Translating the base once and issuing one bus access at that PA reads the
 * wrong bytes in the first case and raises no fault at all in the second.
 *
 * A crossing access is therefore split at the boundary and each side is
 * translated on its own. Two deliberate choices:
 *
 *  - ORDER. The pieces are performed in ascending address order, so the piece
 *    that faults first in program order is the one whose fault is reported.
 *    note_abort keeps the first fault, and the VA handed to it is the address
 *    of the byte that actually faulted — for a second-page fault that is the
 *    page-aligned boundary, which is what DFAR must hold. sleh_abort page-
 *    aligns DFAR and maps in that page; reporting the base instead would map
 *    the page the access already had and spin on the same instruction forever.
 *    This is the same rule the LDM/STM path already follows.
 *
 *  - A STORE THAT FAULTS ON ITS SECOND PAGE LEAVES THE FIRST PAGE WRITTEN.
 *    The ARM abort model is base-*register*-restored: it restores Rn and the
 *    destination registers, and says nothing about undoing memory. Hardware
 *    breaks a crossing access into separate bus transactions and has no way to
 *    retract one that has already completed, so on a real ARM1176 the low bytes
 *    are in memory when the abort is taken. Buffering the whole store and
 *    committing it only if both translations succeed would be *stronger* than
 *    the architecture and would hide exactly the class of bug this exists to
 *    expose. It is also safe under re-execution: the handler maps the second
 *    page and restarts the instruction, which rewrites the same low bytes with
 *    the same values. (A load is different, and is already handled: its result
 *    is returned as a value and every caller drops it when abort_pending is
 *    set, so no register moves.)
 *
 * Only the crossing case is split. A non-crossing access — aligned or not —
 * still goes through one translation and one bus call of its natural width, so
 * device registers keep seeing whole-word transactions.
 *
 * LDRD/STRD and LDM/STM need nothing extra: they are built from per-word
 * mem_r32/mem_w32 calls at ascending addresses, so each word that happens to
 * straddle a boundary is split here, in the right order, by itself.
 *
 * The `priv` argument is explicit because the translation-mode load/stores
 * (LDRT/STRT/LDRBT/STRBT, encoded P==0 && W==1) must translate as *unprivileged*
 * even when executing in a privileged mode. That is precisely the mechanism a
 * kernel uses to touch user memory safely, so getting it wrong would let a
 * guest kernel read pages it should fault on. */
#define ARM_PAGE_MASK 0xfffu

/* Does an n-byte access starting at va span two 4 KB pages? n is 2 or 4, so the
 * access can straddle at most one boundary. */
static inline bool mem_crosses_page(uint32_t va, unsigned n) {
    return ((va & ARM_PAGE_MASK) + n) > (ARM_PAGE_MASK + 1u);
}

/* Byte-at-a-time little-endian read across a page boundary, re-translating at
 * each page. Returns 0 with the abort latched if either side faults. */
static uint32_t mem_read_crossing(arm_cpu_t *c, uint32_t va, unsigned n, bool priv) {
    uint32_t val = 0, page_pa = 0;
    for (unsigned i = 0; i < n; i++) {
        uint32_t a = va + i;
        if (i == 0u || (a & ARM_PAGE_MASK) == 0u) {
            uint32_t pa, f = arm_mmu_translate(c, a, ARM_ACCESS_READ, priv, &pa);
            if (f) { note_abort(c, f, a); return 0; }
            page_pa = pa & ~ARM_PAGE_MASK;
        }
        val |= (uint32_t)c->bus->read8(c->bus->ctx, page_pa | (a & ARM_PAGE_MASK))
               << (8u * i);
    }
    return val;
}

/* The storing mirror. Bytes already written stay written if a later page
 * faults — see the note above. */
static void mem_write_crossing(arm_cpu_t *c, uint32_t va, unsigned n, uint32_t v,
                               bool priv) {
    uint32_t page_pa = 0;
    for (unsigned i = 0; i < n; i++) {
        uint32_t a = va + i;
        if (i == 0u || (a & ARM_PAGE_MASK) == 0u) {
            uint32_t pa, f = arm_mmu_translate(c, a, ARM_ACCESS_WRITE, priv, &pa);
            if (f) { note_abort(c, f, a); return; }
            page_pa = pa & ~ARM_PAGE_MASK;
        }
        c->bus->write8(c->bus->ctx, page_pa | (a & ARM_PAGE_MASK),
                       (uint8_t)(v >> (8u * i)));
    }
}

/*
 * The data-read block cache. See the dread[] comment in arm.h for why this is
 * reads-only and why privilege is in the index as well as the tag.
 */
#define ARM_DREAD_BLK_MASK 0x3ffu

static inline unsigned dread_slot(uint32_t va, bool priv) {
    return (unsigned)(((va >> 10) + (priv ? ARM_DREAD_ENTRIES / 2u : 0u))
                      & (ARM_DREAD_ENTRIES - 1u));
}
static inline uint32_t dread_tag(uint32_t va, bool priv) {
    return (va & ~ARM_DREAD_BLK_MASK) | (priv ? 1u : 0u);
}

/*
 * A host pointer for an n-byte read at va, or NULL to take the slow path.
 *
 * Refuses an access that leaves the block, which is both a permission
 * question (ARMv6 legacy small pages carry one set of AP bits per 1 KB
 * subpage) and a bounds one (host_ram only vouched for these 0x400 bytes, and
 * the last block of RAM has nothing valid after it).
 *
 * Never latches an abort and never installs anything: filling is the slow
 * path's job, because only the slow path has done the walk that proves the
 * access is permitted at all.
 */
static inline const uint8_t *dread_hit(arm_cpu_t *c, uint32_t va, unsigned n,
                                       bool priv) {
#ifdef S5LBOX_NO_DREAD
    /* §9.5's oracle: a pure cache must not change a single retired
     * instruction, so a build with it disabled is the comparison. */
    (void)va; (void)n; (void)priv;
    c->dread_misses++;
    return NULL;
#else
    if (((va & ARM_DREAD_BLK_MASK) + n) > (ARM_DREAD_BLK_MASK + 1u))
        { c->dread_misses++; return NULL; }
    const unsigned slot = dread_slot(va, priv);
    if (!c->dread[slot].host ||
        c->dread[slot].tag != dread_tag(va, priv) ||
        c->dread[slot].gen != c->tlb_gen)
        { c->dread_misses++; return NULL; }
    c->dread_hits++;
    return c->dread[slot].host + (va & ARM_DREAD_BLK_MASK);
#endif
}

/* Install the block a successful walk just resolved, when it is plain RAM.
 * `pa` keeps va's low bits, so the offset within the block is the same on
 * both sides and the pointer lands exactly where bus_read would have. */
static inline void dread_fill(arm_cpu_t *c, uint32_t va, uint32_t pa,
                              bool priv) {
#ifdef S5LBOX_NO_DREAD
    (void)c; (void)va; (void)pa; (void)priv;
    return;
#else
    if (!c->bus->host_ram) return;
    uint8_t *blk = c->bus->host_ram(c->bus->ctx, pa & ~ARM_DREAD_BLK_MASK,
                                    ARM_DREAD_BLK_MASK + 1u);
    if (!blk) return;
    const unsigned slot = dread_slot(va, priv);
    c->dread[slot].host = blk;
    c->dread[slot].tag  = dread_tag(va, priv);
    c->dread[slot].gen  = c->tlb_gen;
#endif
}

/* The write cache repeats dread's proven translation key, but it is live only
 * when the frontend supplies host_ram_write. That separate callback is the
 * permission to bypass bus write observers; host_ram alone is never enough. */
static inline uint8_t *dwrite_hit(arm_cpu_t *c, uint32_t va, unsigned n,
                                  bool priv) {
    if (!c->bus->host_ram_write) return NULL;
    if (((va & ARM_DREAD_BLK_MASK) + n) > (ARM_DREAD_BLK_MASK + 1u)) {
        c->dwrite_misses++;
        return NULL;
    }
    const unsigned slot = dread_slot(va, priv);
    if (!c->dwrite[slot].host ||
        c->dwrite[slot].tag != dread_tag(va, priv) ||
        c->dwrite[slot].gen != c->tlb_gen) {
        c->dwrite_misses++;
        return NULL;
    }
    c->dwrite_hits++;
    return c->dwrite[slot].host + (va & ARM_DREAD_BLK_MASK);
}

static inline void dwrite_fill(arm_cpu_t *c, uint32_t va, uint32_t pa,
                               bool priv) {
    if (!c->bus->host_ram_write) return;
    uint8_t *blk = c->bus->host_ram_write(
        c->bus->ctx, pa & ~ARM_DREAD_BLK_MASK, ARM_DREAD_BLK_MASK + 1u);
    if (!blk) return;
    const unsigned slot = dread_slot(va, priv);
    c->dwrite[slot].host = blk;
    c->dwrite[slot].tag = dread_tag(va, priv);
    c->dwrite[slot].gen = c->tlb_gen;
}

#define MEM_READ(bits)                                                        \
    static uint##bits##_t mem_r##bits##_as(arm_cpu_t *c, uint32_t va, bool priv) { \
        uint32_t original = va;                                               \
        if ((bits) > 8 && (va & ((bits) / 8u - 1u)) != 0u) {                 \
            if ((c->cp15.sctlr & ARM_SCTLR_A) != 0u) {                       \
                note_alignment_abort(c, va, false); return 0;                 \
            }                                                                 \
            if ((c->cp15.sctlr & ARM_SCTLR_U) == 0u)                         \
                va &= ~((bits) / 8u - 1u);                                   \
        }                                                                     \
        uint##bits##_t value;                                                 \
        if (mem_crosses_page(va, (bits) / 8u))                               \
            value = (uint##bits##_t)mem_read_crossing(c, va, (bits) / 8u, priv); \
        else {                                                                \
            const uint8_t *dh = dread_hit(c, va, (bits) / 8u, priv);          \
            if (dh) {                                                         \
                memcpy(&value, dh, (bits) / 8u);                              \
            } else {                                                          \
                uint32_t pa, f = arm_mmu_translate(c, va, ARM_ACCESS_READ,    \
                                                   priv, &pa);                \
                if (f) { note_abort(c, f, va); return 0; }                    \
                value = c->bus->read##bits(c->bus->ctx, pa);                 \
                dread_fill(c, va, pa, priv);                                  \
            }                                                                 \
        }                                                                     \
        if ((bits) == 32 && original != va)                                  \
            value = (uint##bits##_t)legacy_rotate_word((uint32_t)value, original & 3u); \
        return value;                                                         \
    }                                                                         \
    static inline uint##bits##_t mem_r##bits(arm_cpu_t *c, uint32_t va) {     \
        return mem_r##bits##_as(c, va, cpu_is_priv(c));                       \
    }
#define MEM_WRITE(bits)                                                       \
    static void mem_w##bits##_as(arm_cpu_t *c, uint32_t va, uint##bits##_t v, \
                                  bool priv) {                                 \
        if ((bits) > 8 && (va & ((bits) / 8u - 1u)) != 0u) {                 \
            if ((c->cp15.sctlr & ARM_SCTLR_A) != 0u) {                       \
                note_alignment_abort(c, va, true); return;                    \
            }                                                                 \
            if ((c->cp15.sctlr & ARM_SCTLR_U) == 0u)                         \
                va &= ~((bits) / 8u - 1u);                                   \
        }                                                                     \
        if (mem_crosses_page(va, (bits) / 8u)) {                              \
            mem_write_crossing(c, va, (bits) / 8u, v, priv); return;          \
        }                                                                     \
        uint8_t *dw = dwrite_hit(c, va, (bits) / 8u, priv);                   \
        if (dw) { memcpy(dw, &v, (bits) / 8u); return; }                      \
        uint32_t pa, f = arm_mmu_translate(c, va, ARM_ACCESS_WRITE, priv, &pa);\
        if (f) { note_abort(c, f, va); return; }                              \
        c->bus->write##bits(c->bus->ctx, pa, v);                              \
        dwrite_fill(c, va, pa, priv);                                         \
    }                                                                         \
    static inline void mem_w##bits(arm_cpu_t *c, uint32_t va, uint##bits##_t v) {\
        mem_w##bits##_as(c, va, v, cpu_is_priv(c));                           \
    }
/* A byte access can never cross a page — (va & 0xfff) + 1 cannot exceed 0x1000 —
 * so the test is unconditionally false in the 8-bit accessors and the split path
 * there is dead. */
MEM_READ(32)  MEM_READ(16)  MEM_READ(8)
MEM_WRITE(32) MEM_WRITE(16) MEM_WRITE(8)

/* Multiword and coprocessor transfers are stricter than ordinary halfword and
 * word accesses. With U set, any misalignment is an alignment fault regardless
 * of A. In legacy U=0/A=0 mode the ARM1176 aligns the transfer down instead. */
static bool prepare_multiword_address(arm_cpu_t *c, uint32_t *va,
                                      unsigned alignment, bool write) {
    uint32_t mask = alignment - 1u;
    if ((*va & mask) == 0u) return true;
    if ((c->cp15.sctlr & (ARM_SCTLR_U | ARM_SCTLR_A)) != 0u) {
        note_alignment_abort(c, *va, write);
        return false;
    }
    *va &= ~mask;
    return true;
}

/* Exclusive accesses do not get the legacy align-down behavior. If U or A is
 * set the ARM1176 reports an alignment fault; in the legacy 00 configuration
 * they are architecturally UNPREDICTABLE. SWP is the exception and has explicit
 * legacy WLoad+WStore semantics; its decoder handles that separately. */
static bool prepare_sync_address(arm_cpu_t *c, uint32_t va, unsigned alignment,
                                 bool write, arm_status_t *status) {
    if ((va & (alignment - 1u)) == 0u) return true;
    if ((c->cp15.sctlr & (ARM_SCTLR_U | ARM_SCTLR_A)) != 0u) {
        note_alignment_abort(c, va, write);
        *status = ARM_OK;
    } else {
        *status = ARM_UNDEFINED;
    }
    return false;
}

/* VFP uses the coprocessor alignment rules, not the ordinary LDR/STR rules. */
static uint32_t vfp_mem_r32(arm_cpu_t *c, uint32_t va) {
    if (!prepare_multiword_address(c, &va, 4u, false)) return 0u;
    return mem_r32(c, va);
}
static void vfp_mem_w32(arm_cpu_t *c, uint32_t va, uint32_t v) {
    if (!prepare_multiword_address(c, &va, 4u, true)) return;
    mem_w32(c, va, v);
}

/* The VFP unit reaches memory through the interpreter's own translating
 * accessors, so a VLDM that crosses into an unmapped page latches the fault
 * exactly like an LDM would and arm_step converts it to a data abort. */
static const vfp_bus_t g_vfp_bus = { vfp_mem_r32, vfp_mem_w32 };

bool arm_mode_is_valid(uint32_t mode) {
    switch (mode & ARM_CPSR_MODE_MASK) {
        case ARM_MODE_USR: case ARM_MODE_FIQ: case ARM_MODE_IRQ:
        case ARM_MODE_SVC: case ARM_MODE_ABT: case ARM_MODE_UND:
        case ARM_MODE_SYS:
            return true;
        default:
            return false;
    }
}

arm_bank_t arm_bank_of_mode(uint32_t mode) {
    switch (mode & ARM_CPSR_MODE_MASK) {
        case ARM_MODE_FIQ: return ARM_BANK_FIQ;
        case ARM_MODE_IRQ: return ARM_BANK_IRQ;
        case ARM_MODE_SVC: return ARM_BANK_SVC;
        case ARM_MODE_ABT: return ARM_BANK_ABT;
        case ARM_MODE_UND: return ARM_BANK_UND;
        default:           return ARM_BANK_USR;   /* USR and SYS share a bank */
    }
}

void arm_set_mode(arm_cpu_t *c, uint32_t mode) {
    uint32_t old = c->cpsr & ARM_CPSR_MODE_MASK;
    mode &= ARM_CPSR_MODE_MASK;
    if (!arm_mode_is_valid(mode)) return;
    arm_bank_t ob = arm_bank_of_mode(old), nb = arm_bank_of_mode(mode);

    if (ob != nb) {
        /* Park the outgoing bank, then load the incoming one. */
        c->bank_r13[ob] = c->r[13];
        c->bank_r14[ob] = c->r[14];

        /* FIQ additionally banks r8–r12. */
        if (ob == ARM_BANK_FIQ) {
            for (int i = 0; i < 5; i++) { c->fiq_r8_12[i] = c->r[8+i]; c->r[8+i] = c->usr_r8_12[i]; }
        } else if (nb == ARM_BANK_FIQ) {
            for (int i = 0; i < 5; i++) { c->usr_r8_12[i] = c->r[8+i]; c->r[8+i] = c->fiq_r8_12[i]; }
        }

        c->r[13] = c->bank_r13[nb];
        c->r[14] = c->bank_r14[nb];
    }
    c->cpsr = (c->cpsr & ~ARM_CPSR_MODE_MASK) | mode;
}

/*
 * Access a register as the USER bank sees it, whatever mode we are in. Only
 * r8-r14 differ, and only for FIQ (r8-r12) and the privileged modes (r13-r14).
 */
static uint32_t reg_read_user(const arm_cpu_t *c, uint32_t pc, unsigned i) {
    arm_bank_t cur = arm_bank_of_mode(c->cpsr);
    if (i == 15) return pc + 8;
    if (cur == ARM_BANK_USR) return c->r[i];
    if (i == 13) return c->bank_r13[ARM_BANK_USR];
    if (i == 14) return c->bank_r14[ARM_BANK_USR];
    if (i >= 8 && i <= 12 && cur == ARM_BANK_FIQ) return c->usr_r8_12[i - 8];
    return c->r[i];
}

static void reg_write_user(arm_cpu_t *c, unsigned i, uint32_t v) {
    arm_bank_t cur = arm_bank_of_mode(c->cpsr);
    if (cur == ARM_BANK_USR) { c->r[i] = v; return; }
    if (i == 13) { c->bank_r13[ARM_BANK_USR] = v; return; }
    if (i == 14) { c->bank_r14[ARM_BANK_USR] = v; return; }
    if (i >= 8 && i <= 12 && cur == ARM_BANK_FIQ) { c->usr_r8_12[i - 8] = v; return; }
    c->r[i] = v;
}

/* Enter an exception: bank in the handler mode, stash the return address in its
 * LR and the old CPSR in its SPSR, mask interrupts, and vector the PC.
 * CP15 SCTLR.V relocates the vector table to 0xFFFF0000. */
static void take_exception(arm_cpu_t *c, uint32_t vector, uint32_t mode,
                           uint32_t ret_addr, bool mask_fiq, uint32_t *next) {
    uint32_t saved = c->cpsr;
    arm_set_mode(c, mode);
    c->spsr[arm_bank_of_mode(mode)] = saved;
    c->r[14] = ret_addr;
    c->cpsr |= ARM_CPSR_I;                 /* IRQs off on entry */
    if (mask_fiq) c->cpsr |= ARM_CPSR_F;
    /*
     * ARM ARM (ARMv6, DDI0100I) A2.6: entry to Prefetch Abort, Data Abort, IRQ
     * and FIQ additionally sets CPSR.A, masking imprecise aborts; Undefined and
     * SWI leave it alone. Nothing in this machine raises an imprecise abort, so
     * the bit masks nothing — but the guest reads it. Every SPSR XNU saves and
     * every CPSR it prints on a panic carries it, and a nested exception taken
     * inside an abort handler must see A already set in the SPSR it stacks.
     */
    if (vector == ARM_VEC_PREFETCH || vector == ARM_VEC_DATA_ABORT ||
        vector == ARM_VEC_IRQ      || vector == ARM_VEC_FIQ)
        c->cpsr |= ARM_CPSR_A;
    c->cpsr &= ~ARM_CPSR_T;                /* exceptions enter in ARM state */
    /*
     * CPSR.E <- SCTLR.EE (ARM ARM, ARMv6, B4.1.1 and the exception-entry
     * pseudocode in A2.6). Not "leave E alone": the handler must start in the
     * endianness the system control register names, whatever the interrupted
     * code had selected with SETEND. We never set SCTLR.EE and SETEND BE traps,
     * so in practice this always clears E — but writing it as an assignment
     * from EE rather than an unconditional clear keeps the rule intact if a
     * guest ever sets E through MSR CPSR_x, which our MSR mask does permit.
     */
    set_flag(c, ARM_CPSR_E, (c->cp15.sctlr & ARM_SCTLR_EE) != 0);
    /*
     * Taking an exception clears the exclusive monitor. Without this, an
     * interrupt landing between a thread's LDREX and its STREX leaves the tag
     * intact, so the preempted thread's STREX still succeeds after the
     * preempting thread has taken the same lock — two owners of one spinlock.
     * This is invisible until interrupts actually fire, which is why it
     * survived until the timer started working.
     */
    c->excl_valid = false;
    *next = ((c->cp15.sctlr & ARM_SCTLR_V) ? 0xffff0000u : 0u) + vector;
}

/* Complete a data abort latched by the translating memory helpers. This is a
 * helper rather than only arm_step's tail path because unconditional SRS/RFE
 * return directly from their decoder and must take the same exception there. */
static void take_pending_data_abort(arm_cpu_t *c, uint32_t pc) {
    uint32_t vec;
    c->abort_pending = false;
    c->cp15.dfsr = c->abort_fsr;
    c->cp15.dfar = c->abort_far;
    take_exception(c, ARM_VEC_DATA_ABORT, ARM_MODE_ABT, pc + 8u, false, &vec);
    c->r[15] = vec;
}

/* ============================ the Undefined-instruction discrimination =====
 *
 * Everything this core cannot execute returns ARM_UNDEFINED, which stops the
 * machine and prints the encoding and PC. That loudness is the point: it names
 * the next instruction to implement instead of letting the guest run on
 * fabricated results.
 *
 * There is exactly one family of encodings where stopping is WRONG, because
 * the guest is architecturally supposed to see the Undefined exception and
 * handle it. XNU does not leave VFP on. It clears FPEXC.EN and enables VFP
 * lazily, per thread: the first VFP instruction a thread executes is MEANT to
 * be undefined, and the kernel's handler turns VFP on and re-runs it.
 *
 *   _fleh_undef  0xc0067ff8  MRS sp,spsr; TST sp,#0x20; SUBEQ lr,lr,#4;
 *                            SUBNE lr,lr,#2  -> BL _sleh_undef
 *   _sleh_undef  0xc006c184  reads SPSR.T from the saved state, then matches
 *                            the faulting word against six masks (below). On a
 *                            match it calls _get_vfp_enabled (0xc006994c,
 *                            "VMRS r0,FPEXC; AND r1,r0,#0x40000000"):
 *                              FPEXC.EN == 0 -> _vfp_trap -> _vfp_switch,
 *                                 which sets FPEXC.EN and returns WITHOUT
 *                                 advancing PC, so the instruction re-runs;
 *                              FPEXC.EN != 0 -> falls through to
 *                                 EXC_BAD_INSTRUCTION (SIGILL / panic).
 *
 * That second branch is what makes this safe. The kernel itself refuses to
 * treat a VFP encoding as a lazy-enable fault once VFP is already on. So the
 * rule below is not a guess and not a blanket "vector everything":
 *
 *   VFP/SIMD encoding AND VFP currently unavailable -> vector the guest.
 *       Positively identified: this is the lazy-enable path, and the only
 *       thing the guest can do with it is turn VFP on and retry.
 *   VFP/SIMD encoding AND VFP already enabled       -> ARM_UNDEFINED, halt.
 *       We would have to actually compute a floating-point result. Vectoring
 *       here would hand the guest an encoding its own handler classifies as
 *       an illegal instruction, i.e. we would convert "not implemented" into
 *       a SIGILL in launchd and lose the diagnostic entirely.
 *   Anything else                                   -> ARM_UNDEFINED, halt.
 *
 * Because the trap is one-shot per thread, this cannot loop: after the guest
 * enables VFP the retry lands in the second case and stops loudly, naming the
 * exact instruction. Deliberately NOT vectored, though _sleh_undef also
 * recognises them: the ARM/Thumb breakpoints 0xE7FFDEFE / 0xDEFE, which mean
 * the guest has trapped on purpose and we want to see where.
 */

/*
 * The six masks are copied bit for bit out of _sleh_undef's ARM-state branch at
 * 0xc006c368 (its literal pool at 0xc006c50c). Using the kernel's own test
 * rather than one derived from the ARM ARM is deliberate: the set we vector is
 * then, by construction, exactly the set the shipped handler claims it can
 * handle. Anything outside it reaches the guest's "illegal instruction" path,
 * so it must reach ours instead.
 *
 *   0xfe000000/0xf2000000  Advanced SIMD data processing
 *   0x0f000e10/0x0e000a00  VFP data processing            (CDP  cp10/cp11)
 *   0x0e000e00/0x0c000a00  VFP load/store                 (LDC/STC cp10/cp11)
 *   0xff100000/0xf4000000  Advanced SIMD element/structure load/store
 *   0x0f000e10/0x0e000a10  VFP 32-bit register transfer   (MCR/MRC cp10/cp11)
 *   0x0fe00e00/0x0c400a00  VFP 64-bit register transfer   (MCRR/MRRC cp10/11)
 *
 * There is no Thumb counterpart here. _sleh_undef has one (0xc006c420, masks
 * 0xef000e10/0xee000a00 and friends) because the same source builds for
 * ARMv7, but the ARM1176JZF-S is ARMv6 and has no Thumb-2: Thumb state on this
 * part cannot encode a coprocessor instruction at all, so no Thumb encoding
 * can ever be a lazy-VFP fault.
 */
static bool insn_is_vfp_space(uint32_t insn) {
    return (insn & 0xfe000000u) == 0xf2000000u
        || (insn & 0x0f000e10u) == 0x0e000a00u
        || (insn & 0x0e000e00u) == 0x0c000a00u
        || (insn & 0xff100000u) == 0xf4000000u
        || (insn & 0x0f000e10u) == 0x0e000a10u
        || (insn & 0x0fe00e00u) == 0x0c400a00u;
}

/* vfp_cpacr_permits() and vfp_enabled() live in vfp.c (declared in vfp.h):
 * they are half of the availability gate the VFP unit itself applies, and one
 * copy of that rule is the only safe number of copies.
 *
 * True only for the lazy-enable case described above. */
static bool vfp_lazy_enable_trap(const arm_cpu_t *c, uint32_t insn) {
    if (!insn_is_vfp_space(insn)) return false;
    return !vfp_cpacr_permits(c) || !vfp_enabled(c);
}

/*
 * Final disposition of an ARM-state encoding this core did not execute. Either
 * the guest gets its Undefined exception, or we stop.
 *
 * ARM ARM (ARMv6, DDI0100I) A2.6.7: R14_und is the address of the instruction
 * following the undefined one — PC+4 in ARM state, PC+2 in Thumb — the mode
 * becomes Undefined (0b11011), SPSR_und takes the old CPSR, CPSR.I is set and
 * CPSR.T cleared. CPSR.A is NOT set, unlike the abort and interrupt vectors;
 * take_exception already draws that distinction. The kernel confirms the
 * +4/+2 split from the other side: _fleh_undef recovers the faulting PC with
 * "SUBEQ lr,lr,#4 / SUBNE lr,lr,#2" keyed on SPSR.T alone.
 *
 * Only the ARM-state form exists here, and that is not an omission. The Thumb
 * return address would be PC+2, but no Thumb encoding can qualify: ARMv6 Thumb
 * has no coprocessor instructions, so a Thumb undefined instruction is never a
 * lazy-VFP fault and always stops the machine. Writing the +2 case would be
 * unreachable code standing in for a case this part cannot produce.
 */
static arm_status_t take_undefined_instruction(arm_cpu_t *c, uint32_t pc) {
    uint32_t vec;
    take_exception(c, ARM_VEC_UNDEFINED, ARM_MODE_UND, pc + 4u, false, &vec);
    c->r[15] = vec;
    return ARM_OK;
}

static arm_status_t undefined_instruction(arm_cpu_t *c, uint32_t pc,
                                          uint32_t insn) {
    if (!vfp_lazy_enable_trap(c, insn)) return ARM_UNDEFINED;
    return take_undefined_instruction(c, pc);
}

/*
 * CP15 access via MCR (write) / MRC (read).
 *
 * Note on coverage: the architecturally significant registers below are modeled
 * exactly. Cache and TLB maintenance (c7/c8) are accepted as no-ops because we
 * have no caches to flush, and CP15 registers we do not model read as zero and
 * ignore writes. That is a deliberate exception to this core's usual
 * "trap what you don't implement" rule: CP15 is a configuration space that
 * kernels probe widely, and trapping harmless probes would stop a boot dead.
 */
static arm_status_t exec_coprocessor(arm_cpu_t *c, uint32_t pc, uint32_t insn) {
    unsigned cp   = (insn >> 8)  & 0xfu;

    /*
     * CP14 is the debug coprocessor. The kernel probes it during init (reading
     * DBGDIDR) to decide whether a debug unit is present. Reporting "no debug
     * hardware" — zeros — is truthful for this emulator and lets the probe
     * proceed; trapping it would stop a boot over a feature query.
     */
    if (cp == 14) {
        /* Every CP14 register on this part is privileged-access-only, and with
         * the debug unit absent there is nothing here for a user process at
         * all. Answering zeros to unprivileged code would be inventing a
         * permission the hardware does not grant. */
        if (!cpu_is_priv(c)) return ARM_UNDEFINED;
        if ((insn >> 20) & 1u) c->r[(insn >> 12) & 0xfu] = 0;  /* MRC reads 0 */
        return ARM_OK;                                          /* MCR ignored */
    }
    /*
     * CP10/CP11 are the VFP11 coprocessor; this is its MCR/MRC form (VMOV to
     * and from a core register, and VMRS/VMSR). The whole unit — availability,
     * the register file and every instruction — is core/src/arm/vfp.c.
     *
     * Note that ARM_UNDEFINED is not necessarily fatal on this path: arm_step
     * routes it through undefined_instruction(), which vectors the guest when
     * — and only when — the access is one VFP itself is currently refusing.
     */
    if (cp == 10 || cp == 11) return vfp_execute(c, pc, insn, &g_vfp_bus);

    if (cp != 15) return ARM_UNDEFINED;         /* CP10/11/14/15 only */

    bool     load = (insn >> 20) & 1u;          /* 1 = MRC (read), 0 = MCR */
    unsigned opc1 = (insn >> 21) & 7u;
    unsigned crn  = (insn >> 16) & 0xfu;
    unsigned rd   = (insn >> 12) & 0xfu;
    unsigned opc2 = (insn >> 5)  & 7u;
    unsigned crm  = insn & 0xfu;
    arm_cp15_t *p = &c->cp15;

    /*
     * ARM1176JZ-S TRM (DDI0333H) 3.2.22:
     *
     *   MCR p15,0,<Rd>,c7,c0,4      Wait For Interrupt
     *
     * Opcode_1 is zero, the VALUE supplied through Rd is SBZ, and the operation
     * is privileged-only.  Rd is not fixed to r0: shipping XNU's _cpu_idle
     * explicitly clears r2 and emits 0xEE072F90.  It stops following execution
     * until IRQ/FIQ (masked or unmasked), external abort, debug, or reset. This
     * emulator currently has only modeled IRQ/FIQ wake sources, exposed
     * through the optional platform callback.
     * Calling it synchronously is important: when it returns, the MCR can
     * finish normally, so the next arm_step samples the newly asserted line
     * at PC+4 and constructs the architected MCR+8 interrupt return link.
     *
     * A flat CPU harness has no clocked platform and therefore no callback.
     * Preserve its old accepted-no-op behavior rather than blocking the host.
     */
    bool wfi = !load && opc1 == 0u && crn == 7u &&
               opc2 == 4u && crm == 0u;
    if (wfi) {
        if (!cpu_is_priv(c)) return ARM_UNDEFINED;
        /* DDI0333H specifies the transferred value as SBZ.  Rejecting a
         * non-zero value is fail-closed; silently treating it as a wait could
         * park a guest on an encoding whose behavior is not defined. */
        if (reg_read(c, pc, rd) != 0u) return ARM_UNDEFINED;
        if (c->bus && c->bus->wait_for_interrupt)
            (void)c->bus->wait_for_interrupt(c->bus->ctx);
        return ARM_OK;
    }

    /*
     * CP15 is privileged. The ARM1176JZF-S (ARM DDI 0301H, table 3-3) grants
     * User mode exactly three things:
     *
     *   c7   the memory barriers and the cache-maintenance-by-MVA operations —
     *        harmless, and no-ops here because there are no caches to manage;
     *   c13,c0,2  TPIDRURW, read/write — the user-mode thread-ID scratch word;
     *   c13,c0,3  TPIDRURO, READ only — where XNU parks cthread_self. This is
     *        not hypothetical: _thread_set_cthread_self (0xc0061da0) writes it
     *        with "MCR p15,0,r0,c13,c0,3", and libSystem's pthread_self reads
     *        it back from User mode on every lock.
     *
     * Everything else — SCTLR, TTBR0/TTBR1, TTBCR, DACR, the fault registers,
     * FCSEIDR, CONTEXTIDR, TPIDRPRW — is UNDEFINED from User mode. That is not
     * pedantry: without this check a user process can point TTBR0 at a page
     * table it wrote itself, clear SCTLR.M outright, or make DACR name every
     * domain a manager, and our translation layer would obey. This is the same
     * shape of hole as CPS-in-User-mode: unreachable for the whole kernel-only
     * boot, live from the first instruction launchd executes.
     */
    if (!cpu_is_priv(c)) {
        bool allowed = (crn == 7)
                    || (crn == 13 && crm == 0 && opc2 == 2)
                    || (crn == 13 && crm == 0 && opc2 == 3 && load);
        if (!allowed) return ARM_UNDEFINED;
    }

    if (load) {
        uint32_t v = 0;
        switch (crn) {
            case 0: {
                /* CRm selects which identity bank; opc2 the register in it. */
                static const uint32_t id_crm1[8] = {   /* feature registers  */
                    ARM1176_ID_PFR0,  ARM1176_ID_PFR1,
                    ARM1176_ID_DFR0,  ARM1176_ID_AFR0,
                    ARM1176_ID_MMFR0, ARM1176_ID_MMFR1,
                    ARM1176_ID_MMFR2, ARM1176_ID_MMFR3,
                };
                static const uint32_t id_crm2[8] = {   /* ISA attributes     */
                    ARM1176_ID_ISAR0, ARM1176_ID_ISAR1,
                    ARM1176_ID_ISAR2, ARM1176_ID_ISAR3,
                    ARM1176_ID_ISAR4, ARM1176_ID_ISAR5, 0, 0,
                };
                if (crm == 0) {
                    if (opc2 == 0) v = ARM1176_MIDR;
                    else if (opc2 == 1) v = ARM1176_CACHE_TYPE;
                } else if (crm == 1) {
                    v = id_crm1[opc2];
                } else if (crm == 2) {
                    v = id_crm2[opc2];
                }
                break;
            }
            case 1:
                /* Gate CP15 c1 on CRm==0, as c2 is gated below. On the
                 * ARM1176JZF-S CRm==1 is the TrustZone bank — Secure
                 * Configuration, Secure Debug Enable, Non-Secure Access
                 * Control — which this core does not model. Switching on CRn
                 * alone aliased that whole bank onto SCTLR/ACTLR/CPACR. */
                if (crm == 0) {
                    if (opc2 == 0) v = p->sctlr; else if (opc2 == 1) v = p->actlr;
                    else if (opc2 == 2) v = p->cpacr;
                }
                break;
            case 2:
                if (crm == 0) {
                    if (opc2 == 0) v = p->ttbr0; else if (opc2 == 1) v = p->ttbr1;
                    else if (opc2 == 2) v = p->ttbcr;
                }
                break;
            case 3:  v = p->dacr; break;
            case 5:  v = (opc2 == 1) ? p->ifsr : p->dfsr; break;
            case 6:  v = (opc2 == 2) ? p->ifar : p->dfar; break;
            case 13:
                switch (opc2) {
                    case 1:  v = p->context_id; break;
                    case 2:  v = p->tpidrurw;   break;
                    case 3:  v = p->tpidruro;   break;
                    case 4:  v = p->tpidrprw;   break;
                    default: v = p->fcse_pid;   break;
                }
                break;
            default: v = 0; break;              /* unmodelled: reads as zero */
        }
        c->r[rd] = v;
    } else {
        uint32_t v = reg_read(c, pc, rd);
        switch (crn) {
            case 1:
                /* Gate CP15 c1 on CRm==0, as c2 is gated below. Ungated, an
                 * MCR p15,0,Rd,c1,c1,0 aimed at the unmodelled TrustZone bank
                 * would overwrite SCTLR wholesale — and clearing SCTLR.U
                 * switches the whole machine from the ARMv6 unaligned model to
                 * the legacy align-down-and-rotate one, which corrupts loaded
                 * data instead of faulting. */
                if (crm == 0) {
                    if (opc2 == 0) { p->sctlr = v; arm_mmu_tlb_flush(c); }
                    else if (opc2 == 1) p->actlr = v;
                    else if (opc2 == 2) p->cpacr = v;
                }
                break;
            case 2:
                /* ARM1176 TTBCR keeps PD1[5], PD0[4], and N[2:0]. Bit 3 and
                 * bits [31:6] are SBZ/UNP and do not read back. PD0/PD1 are
                 * architecturally visible: they suppress a table walk and
                 * force a section translation fault on a TLB miss. */
                if (crm == 0) {
                    if (opc2 == 0) p->ttbr0 = v; else if (opc2 == 1) p->ttbr1 = v;
                    else if (opc2 == 2) p->ttbcr = v & 0x37u;
                    /* Any of the three re-points or re-splits the walk. This is
                     * the one that matters most in practice: pmap_switch loads
                     * a new user pmap into TTBR0 on every context switch. */
                    arm_mmu_tlb_flush(c);
                }
                break;
            case 3:  p->dacr = v; arm_mmu_tlb_flush(c); break;
            case 5:  if (opc2 == 1) p->ifsr = v; else p->dfsr = v; break;
            case 6:  if (opc2 == 2) p->ifar = v; else p->dfar = v; break;
            case 7:  break;                     /* cache maintenance: no-op */
            /*
             * TLB maintenance. This was a documented no-op for as long as
             * there was no TLB to maintain; now it is the architecture's own
             * hook and the guest is already obliged to use it. Every c8
             * operation -- invalidate all, by MVA, by ASID, D-side, I-side --
             * is honoured as a full flush. Over-flushing is always correct.
             */
            case 8:  arm_mmu_tlb_flush(c); break;
            case 13:
                switch (opc2) {
                    /* The ASID. Translations cached under the old one may not
                     * answer for the new address space. */
                    case 1:  p->context_id = v; arm_mmu_tlb_flush(c); break;
                    case 2:  p->tpidrurw   = v; break;
                    case 3:  p->tpidruro   = v; break;
                    case 4:  p->tpidrprw   = v; break;
                    default: p->fcse_pid   = v; break;
                }
                break;
            default: break;                     /* unmodelled: ignored      */
        }
    }
    return ARM_OK;
}

void arm_reset(arm_cpu_t *cpu, const arm_bus_t *bus) {
    for (int i = 0; i < 16; i++) cpu->r[i] = 0;
    for (int i = 0; i < ARM_BANK_COUNT; i++) {
        cpu->spsr[i] = 0; cpu->bank_r13[i] = 0; cpu->bank_r14[i] = 0;
    }
    for (int i = 0; i < 5; i++) { cpu->fiq_r8_12[i] = 0; cpu->usr_r8_12[i] = 0; }
    { arm_cp15_t z = {0}; cpu->cp15 = z; }   /* MMU off, low vectors */
    /*
     * Generation 1, never 0. Entries carry the generation they were filled in
     * and a hit needs a match; a zeroed cpu has every entry at 0, so starting
     * the counter at 0 would make an empty table read as 4096 valid entries
     * mapping virtual 0. Reset already zeroes the table, so this alone makes
     * it empty.
     */
    memset(cpu->tlb, 0, sizeof cpu->tlb);
    cpu->tlb_gen = 1u;
    /* And the fetch-block cache, which holds a host pointer into the previous
     * machine's RAM. See the fetch_* fields in arm.h. */
    cpu->fetch_host = NULL;
    cpu->fetch_blk  = 0u;
    cpu->fetch_gen  = 0u;
    cpu->fetch_priv = false;
    /* And the data-read block cache, which holds the same kind of pointer. */
    memset(cpu->dread, 0, sizeof cpu->dread);
    memset(cpu->dwrite, 0, sizeof cpu->dwrite);
    cpu->tlb_hits = cpu->tlb_misses = cpu->tlb_flushes = 0;
    cpu->dread_hits = cpu->dread_misses = 0;
    cpu->dwrite_hits = cpu->dwrite_misses = 0;
    cpu->abort_pending = false;
    cpu->abort_fsr = 0;
    cpu->abort_far = 0;
    cpu->irq_line = false;
    cpu->fiq_line = false;
    cpu->excl_valid = false;
    cpu->excl_addr = 0;
    cpu->vfp_fpexc = 0;
    cpu->vfp_fpscr = 0;
    for (int i = 0; i < 32; i++) cpu->vfp_s[i] = 0;
    /* On reset the ARM1176 enters SVC mode with IRQ, FIQ and imprecise aborts
     * disabled and begins execution from the reset vector (0x0, or 0xffff0000
     * with high vectors). We start at 0x0; the machine layer relocates PC as
     * needed. */
    cpu->cpsr   = ARM_MODE_SVC | ARM_CPSR_I | ARM_CPSR_F | ARM_CPSR_A;
    cpu->cycles = 0;
    cpu->bus    = bus;
}

void arm_bus_set_privileged_svc_handler(arm_bus_t *bus,
                                        arm_privileged_svc_handler_t handler,
                                        void *handler_ctx) {
    bus->privileged_svc_handler = handler;
    bus->privileged_svc_ctx = handler != NULL ? handler_ctx : NULL;
}

bool arm_cond_passed(const arm_cpu_t *c, uint32_t cond) {
    bool N = get_flag(c, ARM_CPSR_N), Z = get_flag(c, ARM_CPSR_Z);
    bool C = get_flag(c, ARM_CPSR_C), V = get_flag(c, ARM_CPSR_V);
    switch (cond & 0xf) {
        case 0x0: return Z;              /* EQ */
        case 0x1: return !Z;             /* NE */
        case 0x2: return C;              /* CS/HS */
        case 0x3: return !C;             /* CC/LO */
        case 0x4: return N;              /* MI */
        case 0x5: return !N;             /* PL */
        case 0x6: return V;              /* VS */
        case 0x7: return !V;             /* VC */
        case 0x8: return C && !Z;        /* HI */
        case 0x9: return !C || Z;        /* LS */
        case 0xa: return N == V;         /* GE */
        case 0xb: return N != V;         /* LT */
        case 0xc: return !Z && (N == V); /* GT */
        case 0xd: return Z || (N != V);  /* LE */
        case 0xe: return true;           /* AL */
        default:  return true;           /* 0xf is the unconditional space; arm_step
                                          * intercepts it before this is reached. */
    }
}

/* Reading r15 as an operand yields the address of the current instruction + 8
 * (the classic ARM pipeline offset). `pc` is the current instruction address. */
static inline uint32_t reg_read(const arm_cpu_t *c, uint32_t pc, unsigned n) {
    return (n == 15) ? pc + 8 : c->r[n];
}

/* 32-bit rotate right. */
static inline uint32_t ror32(uint32_t v, unsigned n) {
    n &= 31u;
    return n ? ((v >> n) | (v << (32 - n))) : v;
}

/* Sign-extend without implementation-defined unsigned-to-signed casts. The
 * subtraction is deliberately unsigned, so negative results wrap to their
 * architectural 32-bit two's-complement bit pattern on every C11 host. */
static inline uint32_t sign_extend8(uint32_t v) {
    v &= 0xffu;
    return (v ^ 0x80u) - 0x80u;
}
static inline uint32_t sign_extend16(uint32_t v) {
    v &= 0xffffu;
    return (v ^ 0x8000u) - 0x8000u;
}

/* Clamp an already shifted signed value to an architectural saturation
 * range. `bits` is 1..32 for SSAT and 0..31 for USAT. The caller owns the
 * sticky Q flag, so these helpers only ever set *saturated, never clear it. */
static inline uint32_t signed_saturate(int64_t value, unsigned bits,
                                       bool *saturated) {
    int64_t limit = INT64_C(1) << (bits - 1u);
    int64_t lo = -limit, hi = limit - 1;
    if (value < lo) { *saturated = true; return (uint32_t)lo; }
    if (value > hi) { *saturated = true; return (uint32_t)hi; }
    return (uint32_t)value;
}

static inline uint32_t unsigned_saturate(int64_t value, unsigned bits,
                                         bool *saturated) {
    int64_t hi = bits ? ((INT64_C(1) << bits) - 1) : 0;
    if (value < 0)  { *saturated = true; return 0u; }
    if (value > hi) { *saturated = true; return (uint32_t)hi; }
    return (uint32_t)value;
}

/* Barrel shifter. Returns the shifted value and, via *carry, the shifter
 * carry-out (seeded with the current C flag for the "unaffected" cases). */
static uint32_t barrel_shift(uint32_t val, unsigned type, unsigned amount,
                             bool reg_amount, bool *carry) {
    switch (type & 3u) {
        case 0: /* LSL */
            if (amount == 0) return val;                 /* C unaffected */
            if (amount < 32) { *carry = (val >> (32 - amount)) & 1u; return val << amount; }
            if (amount == 32) { *carry = val & 1u; return 0; }
            *carry = 0; return 0;
        case 1: /* LSR */
            if (amount == 0) {                           /* imm 0 => LSR #32 */
                if (reg_amount) return val;              /* reg 0 => unaffected */
                *carry = (val >> 31) & 1u; return 0;
            }
            if (amount < 32) { *carry = (val >> (amount - 1)) & 1u; return val >> amount; }
            if (amount == 32) { *carry = (val >> 31) & 1u; return 0; }
            *carry = 0; return 0;
        case 2: /* ASR */
            if (amount == 0) {                           /* imm 0 => ASR #32 */
                if (reg_amount) return val;
                *carry = (val >> 31) & 1u; return (val & 0x80000000u) ? 0xffffffffu : 0;
            }
            if (amount < 32) {
                *carry = (val >> (amount - 1)) & 1u;
                return (uint32_t)((int32_t)val >> amount);
            }
            *carry = (val >> 31) & 1u;
            return (val & 0x80000000u) ? 0xffffffffu : 0;
        default: /* ROR / RRX */
            if (amount == 0) {
                if (reg_amount) return val;              /* reg 0 => unaffected */
                /* imm 0 => RRX: 33-bit rotate through carry */
                { uint32_t res = (val >> 1) | ((*carry ? 1u : 0u) << 31);
                  *carry = val & 1u; return res; }
            }
            amount &= 31u;
            if (amount == 0) { *carry = (val >> 31) & 1u; return val; } /* ROR #32 */
            *carry = (val >> (amount - 1)) & 1u;
            return ror32(val, amount);
    }
}

/* Decode the shifter operand of a data-processing instruction. */
static uint32_t dp_operand2(arm_cpu_t *c, uint32_t pc, uint32_t insn, bool *carry) {
    if (insn & (1u << 25)) {                 /* immediate */
        uint32_t imm  = insn & 0xff;
        uint32_t rot  = ((insn >> 8) & 0xf) * 2;
        uint32_t val  = ror32(imm, rot);
        if (rot != 0) *carry = (val >> 31) & 1u; /* rot 0 leaves C unaffected */
        return val;
    } else {                                  /* register, with shift */
        unsigned rm   = insn & 0xf;
        unsigned type = (insn >> 5) & 3u;
        if (insn & (1u << 4)) {               /* register-specified shift amount */
            unsigned rs  = (insn >> 8) & 0xf;
            unsigned amt = reg_read(c, pc, rs) & 0xff;
            uint32_t rmv = reg_read(c, pc, rm);
            return barrel_shift(rmv, type, amt, true, carry);
        } else {                              /* immediate shift amount */
            unsigned amt = (insn >> 7) & 0x1f;
            uint32_t rmv = reg_read(c, pc, rm);
            return barrel_shift(rmv, type, amt, false, carry);
        }
    }
}

/* Arithmetic flag computation for add-with-carry style ops. */
static uint32_t alu_add(arm_cpu_t *c, uint32_t a, uint32_t b, uint32_t cin, bool set) {
    uint64_t u = (uint64_t)a + (uint64_t)b + (uint64_t)cin;
    uint32_t r = (uint32_t)u;
    if (set) {
        set_flag(c, ARM_CPSR_N, (r >> 31) & 1u);
        set_flag(c, ARM_CPSR_Z, r == 0);
        set_flag(c, ARM_CPSR_C, (u >> 32) & 1u);
        set_flag(c, ARM_CPSR_V, (~(a ^ b) & (a ^ r)) >> 31);
    }
    return r;
}
/* Subtraction is add of the one's complement plus carry-in (borrow = !carry). */
static uint32_t alu_sub(arm_cpu_t *c, uint32_t a, uint32_t b, uint32_t cin, bool set) {
    return alu_add(c, a, ~b, cin, set);
}

static void alu_logic_flags(arm_cpu_t *c, uint32_t r, bool carry, bool set) {
    if (!set) return;
    set_flag(c, ARM_CPSR_N, (r >> 31) & 1u);
    set_flag(c, ARM_CPSR_Z, r == 0);
    set_flag(c, ARM_CPSR_C, carry);
    /* V is preserved by logical operations. */
}

/* ------------------------------------------------------------ instr groups */

static arm_status_t exec_data_processing(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                                         uint32_t *next) {
    unsigned opcode = (insn >> 21) & 0xf;
    bool     S      = (insn >> 20) & 1u;
    unsigned rn     = (insn >> 16) & 0xf;
    unsigned rd     = (insn >> 12) & 0xf;

    /*
     * MOVW / MOVT -- ARMv6T2 and later, so absent on the ARM1176 (ARMv6K).
     *
     *   MOVW: cond 0011 0000 imm4 Rd imm12    (0x03000000)
     *   MOVT: cond 0011 0100 imm4 Rd imm12    (0x03400000)
     *
     * These sit in the opcode 0x8..0xb / S==0 hole below, which is why they are
     * tested first: on ARMv7 they are how a 32-bit constant is built, and
     * compilers emit them constantly, so leaving them to fall into the
     * miscellaneous/control decode would misread every one of them.
     *
     * MOVW zero-extends the 16-bit immediate; MOVT replaces only the top half
     * and must leave the bottom half of Rd untouched. Neither sets flags.
     */
    if (c->arch >= ARM_ARCH_V7_SWIFT &&
        ((insn & 0x0fb00000u) == 0x03000000u)) {
        uint32_t imm16 = ((insn >> 4) & 0xf000u) | (insn & 0x0fffu);
        bool top = (insn >> 22) & 1u;
        if (rd == 15u) return ARM_UNDEFINED;   /* UNPREDICTABLE, not a branch */
        c->r[rd] = top ? ((c->r[rd] & 0x0000ffffu) | (imm16 << 16))
                       : imm16;
        return ARM_OK;
    }

    /* Opcodes 0x8..0xb (TST/TEQ/CMP/CMN) are valid comparisons only when S==1.
     * With S==0 this is the ARMv6 miscellaneous/control space (BX, BLX(reg),
     * MRS, MSR, CLZ, ...) — it must NOT fall through to the comparison cases,
     * or e.g. BX would silently execute as TEQ and never branch. */
    if (opcode >= 0x8 && opcode <= 0xb && !S) {
        /* Bit 0 of the target selects the instruction set: set means Thumb. */
        if ((insn & 0x0ffffff0u) == 0x012fff10u) {          /* BX Rm */
            uint32_t t = reg_read(c, pc, insn & 0xf);
            if ((t & 3u) == 2u) return ARM_UNDEFINED;
            if (t & 1u) c->cpsr |= ARM_CPSR_T;
            *next = t & ~1u;
            return ARM_OK;
        }
        if ((insn & 0x0ffffff0u) == 0x012fff30u) {          /* BLX Rm */
            if ((insn & 0xfu) == 15u) return ARM_UNDEFINED;
            uint32_t t = reg_read(c, pc, insn & 0xf);
            if ((t & 3u) == 2u) return ARM_UNDEFINED;
            c->r[14] = pc + 4;
            if (t & 1u) c->cpsr |= ARM_CPSR_T;
            *next = t & ~1u;
            return ARM_OK;
        }

        /*
         * CLZ Rd, Rm — count leading zeros.
         *   cccc 0001 0110 1111 dddd 1111 0001 mmmm
         * Compilers emit this constantly for bit scans, allocators and
         * normalisation, so it is ordinary code rather than a DSP curiosity.
         */
        if ((insn & 0x0fff0ff0u) == 0x016f0f10u) {
            uint32_t v = reg_read(c, pc, insn & 0xfu);
            unsigned n = 0;
            if (!v) n = 32;
            else while (!(v & 0x80000000u)) { v <<= 1; n++; }
            c->r[(insn >> 12) & 0xfu] = n;
            return ARM_OK;
        }

        /*
         * Saturating arithmetic: QADD, QSUB, QDADD, QDSUB.
         *   cccc 0001 0op0 nnnn dddd 0000 0101 mmmm
         * These saturate to INT32_MIN/INT32_MAX rather than wrapping, and set
         * the sticky Q flag (CPSR bit 27) on saturation. Wrapping instead
         * would be silently wrong only at the extremes.
         */
        if ((insn & 0x0f900ff0u) == 0x01000050u) {
            unsigned op = (insn >> 21) & 3u;
            int32_t  m  = (int32_t)reg_read(c, pc, insn & 0xfu);
            int32_t  n  = (int32_t)reg_read(c, pc, (insn >> 16) & 0xfu);
            bool sat = false;
            int64_t acc;

            if (op == 2 || op == 3) {          /* QDADD / QDSUB: double Rn first */
                int64_t d = (int64_t)n * 2;
                if (d > INT32_MAX) { d = INT32_MAX; sat = true; }
                if (d < INT32_MIN) { d = INT32_MIN; sat = true; }
                n = (int32_t)d;
            }
            if (op == 0 || op == 2) acc = (int64_t)m + (int64_t)n;   /* QADD/QDADD */
            else                    acc = (int64_t)m - (int64_t)n;   /* QSUB/QDSUB */

            if (acc > INT32_MAX) { acc = INT32_MAX; sat = true; }
            if (acc < INT32_MIN) { acc = INT32_MIN; sat = true; }

            c->r[(insn >> 12) & 0xfu] = (uint32_t)(int32_t)acc;
            if (sat) c->cpsr |= ARM_CPSR_Q;    /* sticky: only ever set here */
            return ARM_OK;
        }

        {
            bool spsr = (insn >> 22) & 1u;   /* R bit: 0 = CPSR, 1 = SPSR */
            arm_bank_t bank = arm_bank_of_mode(c->cpsr);
            bool has_spsr = (bank != ARM_BANK_USR);

            /* MRS Rd, <psr>: cccc 00010 R 001111 dddd 000000000000 */
            if ((insn & 0x0fbf0fffu) == 0x010f0000u) {
                if (spsr && !has_spsr) return ARM_UNDEFINED; /* no SPSR in USR/SYS */
                c->r[rd] = spsr ? c->spsr[bank] : c->cpsr;
                return ARM_OK;
            }

            /* MSR <psr>_fields, Rm | #imm:
             *   register:  cccc 00010 R 10 mask 1111 00000000 mmmm
             *   immediate: cccc 00110 R 10 mask 1111 rot   imm8
             *
             * The SBO field bits[15:12]==0b1111 must be checked, and for the
             * register form bits[11:4]==0 as well. Without them this mask also
             * swallows CLZ, BKPT, QSUB/QDSUB and the SMULxy/SMLAWy DSP space,
             * which would then silently rewrite CPSR — including the mode
             * field — instead of executing or trapping. */
            if ((insn & 0x0fb0fff0u) == 0x0120f000u ||      /* register  */
                (insn & 0x0fb0f000u) == 0x0320f000u) {      /* immediate */
                unsigned fields = (insn >> 16) & 0xfu;
                uint32_t val;
                if (insn & (1u << 25)) {                     /* immediate form */
                    val = ror32(insn & 0xffu, ((insn >> 8) & 0xfu) * 2u);
                } else {
                    val = reg_read(c, pc, insn & 0xfu);
                }
                uint32_t mask = 0;
                if (fields & 1u) mask |= 0x000000ffu;        /* c: control (mode, I/F/T) */
                if (fields & 2u) mask |= 0x0000ff00u;        /* x: extension            */
                if (fields & 4u) mask |= 0x00ff0000u;        /* s: status               */
                if (fields & 8u) mask |= 0xff000000u;        /* f: flags                */

                if (spsr) {
                    if (!has_spsr) return ARM_UNDEFINED;
                    c->spsr[bank] = (c->spsr[bank] & ~mask) | (val & mask);
                } else {
                    /* User mode may only change the flags byte. */
                    if (!has_spsr && (c->cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR)
                        mask &= 0xff000000u;
                    /* MSR cannot change the execution state: T (and J) are
                     * ignored, otherwise `msr cpsr_c, rX` with an SPSR copied
                     * from interrupted Thumb code would flip the ARM handler
                     * into Thumb (xnu-933 fleh_dec does exactly that). */
                    mask &= ~(ARM_CPSR_T | (1u << 24));
                    uint32_t newcpsr = (c->cpsr & ~mask) | (val & mask);
                    if ((mask & ARM_CPSR_MODE_MASK) != 0u &&
                        !arm_mode_is_valid(newcpsr))
                        return ARM_UNDEFINED;
                    if ((mask & 0x1fu) && (newcpsr & ARM_CPSR_MODE_MASK)
                                          != (c->cpsr & ARM_CPSR_MODE_MASK)) {
                        /* Mode field changed: rebank, then apply the rest. */
                        arm_set_mode(c, newcpsr);
                        c->cpsr = (c->cpsr & ARM_CPSR_MODE_MASK)
                                | (newcpsr & ~ARM_CPSR_MODE_MASK);
                    } else {
                        c->cpsr = newcpsr;
                    }
                }
                return ARM_OK;
            }
        }
        return ARM_UNDEFINED;                    /* unimplemented miscellaneous/control */
    }

    /* ARMv6 register-specified shifts forbid R15 in every operand field. The
     * old ARM7 pipeline quirk sometimes described as a PC+12 read is not a
     * defined result for these ARM1176 encodings. Refuse them before the
     * shifter, ALU, flags, or destination register can change state. */
    if ((insn & (1u << 25)) == 0u && (insn & (1u << 4)) != 0u) {
        unsigned rm = insn & 0xfu;
        unsigned rs = (insn >> 8) & 0xfu;
        if (rd == 15u || rn == 15u || rm == 15u || rs == 15u)
            return ARM_UNDEFINED;
    }

    /* Data-processing writes to PC with S set are exception returns. User and
     * System have no SPSR, and restoring an unimplemented/invalid mode would
     * otherwise make arm_bank_of_mode silently alias it onto the User bank.
     * Reject both cases before arithmetic changes flags or PC. */
    bool writes_result = opcode < 8u || opcode >= 12u;
    if (writes_result && rd == 15u && S) {
        arm_bank_t b = arm_bank_of_mode(c->cpsr);
        if (b == ARM_BANK_USR || !arm_mode_is_valid(c->spsr[b]))
            return ARM_UNDEFINED;
    }

    bool shifter_carry = get_flag(c, ARM_CPSR_C);
    uint32_t op2 = dp_operand2(c, pc, insn, &shifter_carry);
    uint32_t a   = reg_read(c, pc, rn);
    uint32_t cin = get_flag(c, ARM_CPSR_C) ? 1u : 0u;
    uint32_t res = 0;
    bool     writes = true;

    switch (opcode) {
        case 0x0: res = a & op2;  alu_logic_flags(c, res, shifter_carry, S); break; /* AND */
        case 0x1: res = a ^ op2;  alu_logic_flags(c, res, shifter_carry, S); break; /* EOR */
        case 0x2: res = alu_sub(c, a, op2, 1, S);        break;                      /* SUB */
        case 0x3: res = alu_sub(c, op2, a, 1, S);        break;                      /* RSB */
        case 0x4: res = alu_add(c, a, op2, 0, S);        break;                      /* ADD */
        case 0x5: res = alu_add(c, a, op2, cin, S);      break;                      /* ADC */
        case 0x6: res = alu_sub(c, a, op2, cin, S);      break;                      /* SBC */
        case 0x7: res = alu_sub(c, op2, a, cin, S);      break;                      /* RSC */
        case 0x8: (void)(a & op2);  alu_logic_flags(c, a & op2, shifter_carry, true); writes = false; break; /* TST */
        case 0x9: alu_logic_flags(c, a ^ op2, shifter_carry, true); writes = false; break;               /* TEQ */
        case 0xa: alu_sub(c, a, op2, 1, true);   writes = false; break;             /* CMP */
        case 0xb: alu_add(c, a, op2, 0, true);   writes = false; break;             /* CMN */
        case 0xc: res = a | op2;  alu_logic_flags(c, res, shifter_carry, S); break; /* ORR */
        case 0xd: res = op2;      alu_logic_flags(c, res, shifter_carry, S); break; /* MOV */
        case 0xe: res = a & ~op2; alu_logic_flags(c, res, shifter_carry, S); break; /* BIC */
        case 0xf: res = ~op2;     alu_logic_flags(c, res, shifter_carry, S); break; /* MVN */
    }

    if (writes) {
        c->r[rd] = res;
        if (rd == 15) {
            /* Writing PC with S set is an exception return (the classic
             * "SUBS pc, lr, #4" ending an IRQ handler): CPSR comes from the
             * current mode's SPSR, which also undoes the flag update above. */
            if (S) {
                arm_bank_t b = arm_bank_of_mode(c->cpsr);
                if (b != ARM_BANK_USR) {
                    uint32_t s = c->spsr[b];
                    arm_set_mode(c, s);
                    c->cpsr = (c->cpsr & ARM_CPSR_MODE_MASK) | (s & ~ARM_CPSR_MODE_MASK);
                }
            }
            /*
             * Align for the instruction set we are landing in, not the one we
             * came from. When S is set the SPSR restore above has just put T
             * back, so a handler returning to interrupted Thumb code must
             * align to a halfword. Forcing word alignment silently rewinds the
             * resume address by 2 whenever the interrupted Thumb instruction
             * sat at an odd halfword, and the guest re-executes the preceding
             * instruction — which is how a zone free ended up unlocking a
             * mutex at address 1.
             *
             * Safe for the ordinary S==0 case: in ARM state T is clear, so the
             * mask stays ~3u and MOV pc,Rm correctly does not interwork.
             */
            *next = res & ((c->cpsr & ARM_CPSR_T) ? ~1u : ~3u);
        }
    }
    return ARM_OK;
}

static arm_status_t exec_branch(arm_cpu_t *c, uint32_t pc, uint32_t insn, uint32_t *next) {
    int32_t off = (int32_t)(insn << 8) >> 6;   /* sign-extend 24-bit, <<2 */
    if (insn & (1u << 24)) c->r[14] = pc + 4;   /* BL: LR = return address */
    *next = (pc + 8 + off) & ~3u;
    return ARM_OK;
}

static arm_status_t exec_single_transfer(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                                         uint32_t *next) {
    bool I = (insn >> 25) & 1u;  /* 0 => immediate offset, 1 => register offset */
    bool P = (insn >> 24) & 1u;  /* pre/post index */
    bool U = (insn >> 23) & 1u;  /* add/subtract offset */
    bool B = (insn >> 22) & 1u;  /* byte/word */
    bool W = (insn >> 21) & 1u;  /* writeback */
    bool L = (insn >> 20) & 1u;  /* load/store */
    unsigned rn = (insn >> 16) & 0xf;
    unsigned rd = (insn >> 12) & 0xf;
    bool writeback = !P || W;
    bool load_same_base_zero_post =
        L && !I && !P && !W && rn == rd && rd != 15u &&
        (insn & 0xfffu) == 0u;

    /* Addressing mode 2 marks these operand combinations UNPREDICTABLE. Trap
     * them before calculating an address or touching the bus rather than
     * guessing which result wins. There is one bounded ARMv6-era exception:
     * legacy startup code uses LDR Rd,[Rd],#0. Its writeback is arithmetically
     * a no-op, and the loaded value wins. Keep every nonzero, register-offset,
     * pre-indexed and store overlap fail-closed. An R15 offset/base has no
     * defined writeback value, and byte transfers cannot name R15 as data. */
    if (writeback &&
        (rn == 15u || (rn == rd && !load_same_base_zero_post)))
        return ARM_UNDEFINED;
    if (I && (insn & 0xfu) == 15u) return ARM_UNDEFINED;
    if (B && rd == 15u) return ARM_UNDEFINED;
    if (L && !P && W && rd == 15u) return ARM_UNDEFINED; /* LDRT pc */

    uint32_t offset;
    if (!I) {
        offset = insn & 0xfff;
    } else {
        unsigned rm = insn & 0xf, type = (insn >> 5) & 3u, amt = (insn >> 7) & 0x1f;
        bool carry = get_flag(c, ARM_CPSR_C);
        offset = barrel_shift(reg_read(c, pc, rm), type, amt, false, &carry);
    }

    uint32_t base = reg_read(c, pc, rn);
    uint32_t addr = P ? (U ? base + offset : base - offset) : base;

    /* P==0 && W==1 is the translation form (LDRT/STRT/LDRBT/STRBT): the access
     * is performed as if unprivileged. */
    bool priv = (!P && W) ? false : cpu_is_priv(c);

    if (L) { /* load */
        /* LDR-to-PC is not an ordinary WLoad. With SCTLR.A set it raises the
         * alignment fault; with A clear, both legacy and ARMv6 unaligned modes
         * classify the operation as UNPREDICTABLE. Never rotate/fix it up or
         * touch memory in either case. */
        if (rd == 15u && (addr & 3u) != 0u) {
            if ((c->cp15.sctlr & ARM_SCTLR_A) != 0u) {
                note_alignment_abort(c, addr, false);
                return ARM_OK;
            }
            return ARM_UNDEFINED;
        }
        uint32_t val = B ? mem_r8_as(c, addr, priv)
                         : mem_r32_as(c, addr, priv);
        /* Base Restored Abort Model: on a fault the destination and base must
         * be left untouched so the handler can fix the mapping and re-execute. */
        if (c->abort_pending) return ARM_OK;
        if (rd == 15u && (val & 3u) == 2u) return ARM_UNDEFINED;
        c->r[rd] = val;
        if (rd == 15) {                      /* LDR to PC interworks too */
            if (val & 1u) c->cpsr |= ARM_CPSR_T; else c->cpsr &= ~ARM_CPSR_T;
            *next = val & (uint32_t)((val & 1u) ? ~1u : ~3u);
        }
    } else {  /* store */
        /* ARM STR stores PC as the address of the instruction plus 12. This is
         * one of the few contexts where R15 does not have its usual PC+8
         * operand value. */
        uint32_t val = (rd == 15u) ? pc + 12u : c->r[rd];
        if (B) mem_w8_as(c, addr, (uint8_t)val, priv);
        else   mem_w32_as(c, addr, val, priv);
        if (c->abort_pending) return ARM_OK;
    }

    /* Writeback: post-indexed always writes back; pre-indexed writes back if W. */
    if (!P) {
        addr = U ? base + offset : base - offset;
        if (!load_same_base_zero_post) c->r[rn] = addr;
    }
    else if (W) { c->r[rn] = addr; }
    return ARM_OK;
}

/* Extra load/store: LDRH/STRH/LDRSB/LDRSH (halfword and sign-extending forms)
 * plus the doubleword pair LDRD/STRD.
 * Encoding: cccc 000 P U I W L nnnn tttt iiii 1SH1 iiii, with SH != 00.
 * I selects an 8-bit immediate offset (split across bits[11:8] and bits[3:0])
 * versus a register offset in Rm.
 *
 * The doubleword forms are the two encodings with L == 0 and SH != 01: they
 * reuse the L bit as part of the operation, so LDRD (SH == 10) is a LOAD even
 * though L is clear. Everything else about them — addressing mode 3, the
 * operand restrictions, the writeback rules — is shared with the halfword
 * forms and is deliberately not duplicated below. */
static arm_status_t exec_extra_transfer(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                                        uint32_t *next) {
    bool P = (insn >> 24) & 1u;
    bool U = (insn >> 23) & 1u;
    bool I = (insn >> 22) & 1u;
    bool W = (insn >> 21) & 1u;
    bool L = (insn >> 20) & 1u;
    unsigned rn = (insn >> 16) & 0xf;
    unsigned rd = (insn >> 12) & 0xf;
    unsigned sh = (insn >> 5) & 3u;
    bool writeback = !P || W;
    bool doubleword = !L && sh != 1u;      /* LDRD (sh==2) / STRD (sh==3) */

    /* Every halfword/sign-extending form reserves R15 as the destination; do
     * not let a malformed encoding branch through a truncated load. */
    if (rd == 15u) return ARM_UNDEFINED;

    /* Addressing mode 3 has no unprivileged P=0,W=1 form. In the register
     * form bits[11:8] are SBZ and Rm cannot be R15. As in addressing mode 2,
     * a writeback base cannot be R15 or alias the data register. Reject every
     * case before even reading device-like memory. */
    if (!P && W) return ARM_UNDEFINED;
    if (writeback && (rn == 15u || rn == rd)) return ARM_UNDEFINED;
    if (!I && ((((insn >> 8) & 0xfu) != 0u) || (insn & 0xfu) == 15u))
        return ARM_UNDEFINED;

    /* LDRD/STRD name a register PAIR, Rd and Rd+1, so Rd must be even — an odd
     * Rd has no defined pairing — and cannot be R14, which would make R15 the
     * second half. (Rd == 15 is already refused above, so those two rules
     * together are exactly "neither half may be R15".) A writeback base cannot
     * alias EITHER half; the Rd case is covered above, Rd+1 is here. All are
     * UNPREDICTABLE, so trap before an address is formed or the bus is touched. */
    if (doubleword) {
        if ((rd & 1u) != 0u || rd == 14u) return ARM_UNDEFINED;
        if (writeback && rn == rd + 1u)   return ARM_UNDEFINED;
    }

    uint32_t offset = I ? ((((insn >> 8) & 0xf) << 4) | (insn & 0xf))
                        : reg_read(c, pc, insn & 0xf);
    uint32_t base = reg_read(c, pc, rn);
    uint32_t addr = P ? (U ? base + offset : base - offset) : base;

    if ((sh == 1u || (L && sh == 3u)) &&
        legacy_halfword_unpredictable(c, addr))
        return ARM_UNDEFINED;

    if (doubleword) {
        bool store = sh == 3u;             /* sh==2 is LDRD, sh==3 is STRD */

        /*
         * ARMv6 relaxed the ARMv5TE doubleword-alignment requirement: the pair
         * is two ordinary word accesses and needs only word alignment, under
         * the same U/A matrix LDM/STM uses (ARM1176 TRM DDI0301H, 6.11). With
         * U set, a word-aligned-but-not-doubleword-aligned address is legal;
         * in the legacy U=0/A=0 configuration the transfer aligns down.
         *
         * The aligned-down value is kept in a local so it cannot leak into the
         * pre-indexed writeback below, exactly as LDM/STM computes its own
         * writeback independently of the address it transfers from.
         */
        uint32_t xfer = addr;
        if (!prepare_multiword_address(c, &xfer, 4u, store)) return ARM_OK;

        if (store) {
            mem_w32(c, xfer, reg_read(c, pc, rd));
            if (c->abort_pending) return ARM_OK;   /* second word not issued */
            mem_w32(c, xfer + 4u, reg_read(c, pc, rd + 1u));
            if (c->abort_pending) return ARM_OK;
        } else {
            /* Base-restored abort model: both halves are buffered and committed
             * only once both accesses have succeeded, so a fault on either word
             * leaves the pair and the base exactly as the handler found them. */
            uint32_t lo = mem_r32(c, xfer);
            if (c->abort_pending) return ARM_OK;   /* second word not issued */
            uint32_t hi = mem_r32(c, xfer + 4u);
            if (c->abort_pending) return ARM_OK;
            c->r[rd]      = lo;
            c->r[rd + 1u] = hi;
        }
    } else if (L) {
        uint32_t val;
        switch (sh) {
            case 1: val = mem_r16(c, addr); break;   /* LDRH  */
            case 2: val = (uint32_t)(int32_t)(int8_t) mem_r8(c, addr); break;  /* LDRSB */
            case 3: val = (uint32_t)(int32_t)(int16_t)mem_r16(c, addr); break; /* LDRSH */
            default: return ARM_UNDEFINED;
        }
        if (c->abort_pending) return ARM_OK;      /* base-restored abort model */
        c->r[rd] = val;
        if (rd == 15) *next = val & ~3u;
    } else {
        mem_w16(c, addr, (uint16_t)reg_read(c, pc, rd)); /* STRH */
        if (c->abort_pending) return ARM_OK;
    }

    if (!P) { addr = U ? base + offset : base - offset; c->r[rn] = addr; }
    else if (W) { c->r[rn] = addr; }
    return ARM_OK;
}

/* Block data transfer: LDM/STM in all four addressing modes.
 * Encoding: cccc 100 P U S W L nnnn register_list(16).
 * Registers always move in increasing register order at increasing addresses;
 * P/U only decide where the run of addresses starts. */
static arm_status_t exec_block_transfer(arm_cpu_t *c, uint32_t pc, uint32_t insn,
                                        uint32_t *next) {
    bool P = (insn >> 24) & 1u;
    bool U = (insn >> 23) & 1u;
    bool S = (insn >> 22) & 1u;
    bool W = (insn >> 21) & 1u;
    bool L = (insn >> 20) & 1u;
    unsigned rn = (insn >> 16) & 0xf;
    uint32_t list = insn & 0xffffu;

    if (list == 0) return ARM_UNDEFINED;     /* empty list is unpredictable */
    if (rn == 15u) return ARM_UNDEFINED;     /* PC as a multiple-transfer base */

    /*
     * S=1 has two meanings. With PC in a load list it is an exception return
     * (CPSR <- SPSR). Otherwise it transfers the USER bank regardless of the
     * current mode — which is how a kernel saves and restores user context on
     * exception entry. Real XNU does exactly that with "STMIA sp,{r0-r14}^".
     */
    bool restore_cpsr = false;
    bool user_bank    = false;
    if (S) {
        if (L && (list & (1u << 15))) restore_cpsr = true;
        else                          user_bank = true;
    }

    if (restore_cpsr) {
        arm_bank_t b = arm_bank_of_mode(c->cpsr);
        if (b == ARM_BANK_USR || !arm_mode_is_valid(c->spsr[b]))
            return ARM_UNDEFINED;
    }

    /* LDM(2)/STM(2) are specifically privileged user-bank transfers. User and
     * System modes have no distinct privileged/user register view for this
     * encoding, so the architecture marks both cases UNPREDICTABLE. Refuse the
     * instruction before it can touch attacker-selected memory. */
    if (user_bank) {
        uint32_t mode = c->cpsr & ARM_CPSR_MODE_MASK;
        if (mode == ARM_MODE_USR || mode == ARM_MODE_SYS)
            return ARM_UNDEFINED;
    }

    /*
     * The user-bank forms are LDM(2)/STM(2), and the architecture states their
     * operand restriction flatly: "the W bit must not be set" (ARM ARM, ARMv6,
     * A4.1.21 / A4.1.42) — W == 1 here is UNPREDICTABLE. There is no defensible
     * answer to give: writeback would have to land in EITHER the current mode's
     * Rn or the user bank's, and the two choices differ precisely when Rn is
     * r13, which is the only base XNU ever uses with this form. Guessing one
     * would corrupt a stack pointer silently. Trap instead, and name it.
     *
     * Nothing in the shipped kernel needs this: every `^` transfer in xnu-1357's
     * ARM code — _fleh_swi+0x8, _fleh_undef+0x20, _fleh_prefabt+0x18,
     * _fleh_dataabt+0x18, _fleh_irq+0x18 and _thread_exception_return+0x58 —
     * encodes W == 0 (0xe8cd7fff / 0xe8dd7fff).
     */
    if (user_bank && W) return ARM_UNDEFINED;

    /* Writeback with an ARM LDM base in the list has no defined final Rn.
     * For STM it is defined only when Rn is the lowest register stored; in
     * every other ordering the value emitted for Rn is UNPREDICTABLE. */
    if (W && (list & (1u << rn)) != 0u) {
        if (L) return ARM_UNDEFINED;
        uint32_t lower = rn == 0u ? 0u : ((1u << rn) - 1u);
        if ((list & lower) != 0u) return ARM_UNDEFINED;
    }

    unsigned n = 0;
    for (unsigned i = 0; i < 16; i++) if (list & (1u << i)) n++;

    uint32_t base = reg_read(c, pc, rn);
    uint32_t addr, wb;
    if (U) { addr = P ? base + 4u : base;                  wb = base + 4u * n; }
    else   { addr = P ? base - 4u * n : base - 4u * n + 4u; wb = base - 4u * n; }

    if (!prepare_multiword_address(c, &addr, 4u, !L)) return ARM_OK;

    /* Loaded values are buffered and only committed once every access has
     * succeeded: the base-restored abort model requires that a data abort part
     * way through an LDM leaves the register file (and especially Rn) as it
     * was, so the handler can map the page and re-execute the instruction. */
    uint32_t loaded[16];
    for (unsigned i = 0; i < 16; i++) {
        if (!(list & (1u << i))) continue;
        if (L) {
            loaded[i] = mem_r32(c, addr);
        } else {
            /* Like STR, storing R15 in an STM writes PC+12. The S/user-bank
             * form does not change that value. */
            uint32_t value = (i == 15u) ? pc + 12u
                              : user_bank ? reg_read_user(c, pc, i)
                                          : c->r[i];
            mem_w32(c, addr, value);
        }
        if (c->abort_pending) return ARM_OK;
        addr += 4u;
    }

    if (L) {
        /* Plain LDM interworks from bit 0, so an ARM destination ending in
         * 0b10 is not representable. The exception-return LDM(3) is kept on
         * its established restored-state align-down path below. */
        if (!restore_cpsr && (list & (1u << 15)) != 0u &&
            (loaded[15] & 3u) == 2u)
            return ARM_UNDEFINED;
        /* LDM(3) takes instruction state from SPSR, not from loaded PC bit 0.
         * Returning to ARM with bit 1 set is therefore an unrepresentable
         * halfword target, not a value to round down. Validate before any
         * destination register or writeback is committed. */
        if (restore_cpsr && (list & (1u << 15)) != 0u) {
            uint32_t s = c->spsr[arm_bank_of_mode(c->cpsr)];
            if ((s & ARM_CPSR_T) == 0u && (loaded[15] & 2u) != 0u)
                return ARM_UNDEFINED;
        }
        for (unsigned i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            if (user_bank) { reg_write_user(c, i, loaded[i]); continue; }
            c->r[i] = loaded[i];
            if (i == 15 && !restore_cpsr) {
                /* ARMv5 and later: loading PC INTERWORKS. Bit 0 selects the
                 * instruction set, so "POP {..,pc}" returning to a Thumb caller
                 * must switch to Thumb. Masking bit 0 off without setting T
                 * leaves the CPU decoding Thumb bytes as ARM — which is exactly
                 * how real XNU ended up executing garbage and taking a
                 * spurious SWI. */
                if (loaded[i] & 1u) c->cpsr |= ARM_CPSR_T;
                else                c->cpsr &= ~ARM_CPSR_T;
                *next = loaded[i] & (uint32_t)((loaded[i] & 1u) ? ~1u : ~3u);
            }
            /* The exception-return form (LDM {...,pc}^) is deliberately NOT
             * handled here: there, bit 0 of the loaded word is not a Thumb
             * selector — T comes from the SPSR, which has not been restored
             * yet. It is finished below, after the restore. */
        }
    }

    /* On LDM with Rn in the list, the loaded value wins over writeback. */
    if (W && !(L && (list & (1u << rn)))) c->r[rn] = wb;

    /* Exception return: writeback lands in the handler's banked Rn above,
     * then CPSR <- SPSR rebanks us into the interrupted mode. */
    if (restore_cpsr) {
        arm_bank_t b = arm_bank_of_mode(c->cpsr);
        uint32_t s = c->spsr[b];
        arm_set_mode(c, s);
        c->cpsr = (c->cpsr & ARM_CPSR_MODE_MASK) | (s & ~ARM_CPSR_MODE_MASK);

        /* Now that T is back, finish the PC write for LDM {...,pc}^. The
         * restored T decides both the instruction set and the alignment; bit 0
         * of the loaded word is part of the address, not a selector. Doing
         * this before the restore — as the plain-LDM path above does — would
         * both interwork off the wrong bit and align for the wrong state. */
        if (L && (list & (1u << 15)))
            *next = loaded[15] & (uint32_t)((c->cpsr & ARM_CPSR_T) ? ~1u : ~3u);
    }
    return ARM_OK;
}

/*
 * ARMv6 media space: the extend and byte-reverse families. Real XNU and its
 * userland use these in ordinary compiled code, so they are not optional.
 *
 *   extend: cccc 0110 1 op nnnn dddd rr00 0111 mmmm
 *           Rn == 15 is the plain form; otherwise the result is added to Rn.
 *           rr rotates the source right by rr*8 before extracting.
 */
static arm_status_t exec_media(arm_cpu_t *c, uint32_t pc, uint32_t insn) {
    if ((insn & 0x0f8003f0u) == 0x06800070u) {
        unsigned op  = (insn >> 20) & 0xfu;
        unsigned rn  = (insn >> 16) & 0xfu;
        unsigned rd  = (insn >> 12) & 0xfu;
        unsigned rm  = insn & 0xfu;
        unsigned rot = (insn >> 10) & 3u;
        uint32_t v;
        uint32_t res;

        /* ARM ARM DDI0100I A4-216..226 and A4-274..284: Rd and Rm == PC are
         * UNPREDICTABLE. Refuse those encodings instead of turning an extend
         * into an accidental branch or reading the pipelined PC. Rn == PC
         * names the non-adding form and is therefore valid. */
        if (rd == 15u || rm == 15u) return ARM_UNDEFINED;
        v = ror32(reg_read(c, pc, rm), rot * 8u);

        /* SXTB16/SXTAB16 and UXTB16/UXTAB16 operate as two independent
         * 16-bit lanes. Each selected byte is extended to 16 bits, then (for
         * Rn != PC) added to the corresponding Rn halfword modulo 2^16. A
         * carry from the low lane must never leak into the high lane. */
        if (op == 0x8u || op == 0xcu) {
            uint16_t lo_ext, hi_ext;
            uint16_t lo_base = 0u, hi_base = 0u;
            if (op == 0x8u) {                                  /* SXT(A)B16 */
                lo_ext = (uint16_t)sign_extend8(v);
                hi_ext = (uint16_t)sign_extend8(v >> 16);
            } else {                                           /* UXT(A)B16 */
                lo_ext = (uint16_t)(uint8_t)v;
                hi_ext = (uint16_t)(uint8_t)(v >> 16);
            }
            if (rn != 15u) {
                uint32_t base = reg_read(c, pc, rn);
                lo_base = (uint16_t)base;
                hi_base = (uint16_t)(base >> 16);
            }
            res = (uint32_t)(uint16_t)(lo_base + lo_ext)
                | ((uint32_t)(uint16_t)(hi_base + hi_ext) << 16);
            c->r[rd] = res;
            return ARM_OK;
        }

        switch (op) {
            case 0xa: res = sign_extend8(v);                           break; /* SXTB  */
            case 0xb: res = sign_extend16(v);                          break; /* SXTH  */
            case 0xe: res = v & 0xffu;                                break; /* UXTB  */
            case 0xf: res = v & 0xffffu;                              break; /* UXTH  */
            default:  return ARM_UNDEFINED;
        }
        if (rn != 15) res += reg_read(c, pc, rn);          /* accumulate form */
        c->r[rd] = res;
        return ARM_OK;
    }

    /* REV / REV16 / REVSH */
    if ((insn & 0x0fff0ff0u) == 0x06bf0f30u) {             /* REV */
        unsigned rd = (insn >> 12) & 0xfu, rm = insn & 0xfu;
        if (rd == 15u || rm == 15u) return ARM_UNDEFINED;
        uint32_t v = reg_read(c, pc, rm);
        c->r[rd] =
            ((v & 0xffu) << 24) | ((v & 0xff00u) << 8)
          | ((v >> 8) & 0xff00u) | ((v >> 24) & 0xffu);
        return ARM_OK;
    }
    if ((insn & 0x0fff0ff0u) == 0x06bf0fb0u) {             /* REV16 */
        unsigned rd = (insn >> 12) & 0xfu, rm = insn & 0xfu;
        if (rd == 15u || rm == 15u) return ARM_UNDEFINED;
        uint32_t v = reg_read(c, pc, rm);
        c->r[rd] =
            ((v & 0x00ffu) << 8) | ((v & 0xff00u) >> 8)
          | ((v & 0x00ff0000u) << 8) | ((v & 0xff000000u) >> 8);
        return ARM_OK;
    }
    if ((insn & 0x0fff0ff0u) == 0x06ff0fb0u) {             /* REVSH */
        unsigned rd = (insn >> 12) & 0xfu, rm = insn & 0xfu;
        if (rd == 15u || rm == 15u) return ARM_UNDEFINED;
        uint32_t v = reg_read(c, pc, rm);
        uint16_t h = (uint16_t)(((v & 0xffu) << 8) | ((v >> 8) & 0xffu));
        c->r[rd] = sign_extend16(h);
        return ARM_OK;
    }
    /*
     * ARMv6 parallel (SIMD) addition and subtraction.
     *
     *   cccc 0110 0aaa nnnn dddd 1111 ooo1 mmmm
     *
     * aaa selects the class and ooo the lane operation. The whole family is
     * implemented together rather than just the encoding that stopped a boot:
     * these differ only in how one lane result is reduced, so implementing one
     * and leaving its neighbours to trap would hide the next stop behind an
     * identical shape. That is the same reasoning the paired-extend family was
     * implemented under.
     *
     * Lane arithmetic is done in wider signed/unsigned intermediates and only
     * then narrowed, so a carry or borrow can never leak between lanes.
     *
     * Only the plain signed and unsigned classes write GE; the saturating and
     * halving classes leave it alone, which is what SEL then reads.
     */
    if ((insn & 0x0f800f10u) == 0x06000f10u) {
        unsigned cls = (insn >> 20) & 7u;    /* 1 S, 2 Q, 3 SH, 5 U, 6 UQ, 7 UH */
        unsigned op  = (insn >> 5) & 7u;
        unsigned rn  = (insn >> 16) & 0xfu;
        unsigned rd  = (insn >> 12) & 0xfu;
        unsigned rm  = insn & 0xfu;
        if (cls == 0u || cls == 4u) return ARM_UNDEFINED;
        if (op == 5u || op == 6u)   return ARM_UNDEFINED;
        /* Rd, Rn or Rm == PC is UNPREDICTABLE; refuse rather than invent. */
        if (rd == 15u || rn == 15u || rm == 15u) return ARM_UNDEFINED;

        uint32_t a = reg_read(c, pc, rn), b = reg_read(c, pc, rm);
        bool byte    = (op == 4u || op == 7u);
        bool is_sub  = (op == 3u || op == 7u);
        bool exch_as = (op == 1u);           /* ASX: sub low, add high  */
        bool exch_sa = (op == 2u);           /* SAX: add low, sub high  */
        bool sgn     = (cls == 1u || cls == 2u || cls == 3u);
        bool sat     = (cls == 2u || cls == 6u);
        bool halve   = (cls == 3u || cls == 7u);
        bool writes_ge = (cls == 1u || cls == 5u);

        unsigned lanes = byte ? 4u : 2u;
        unsigned width = byte ? 8u : 16u;
        uint32_t result = 0, ge = 0;

        for (unsigned i = 0; i < lanes; i++) {
            unsigned sh = i * width;
            uint32_t am = (a >> sh) & (byte ? 0xffu : 0xffffu);
            /* The exchange forms pair lane 0 with lane 1 of the other operand;
             * they are defined only for the halfword operations. */
            unsigned bi = (!byte && (exch_as || exch_sa)) ? (1u - i) : i;
            uint32_t bm = (b >> (bi * width)) & (byte ? 0xffu : 0xffffu);

            bool sub = is_sub;
            if (exch_as) sub = (i == 0u);    /* low subtracts, high adds */
            if (exch_sa) sub = (i == 1u);    /* low adds, high subtracts */

            int64_t  sv;                      /* signed full-precision lane   */
            uint64_t uv;                      /* unsigned full-precision lane */
            int64_t  as = sgn ? (byte ? (int64_t)(int8_t)am
                                      : (int64_t)(int16_t)am)
                              : (int64_t)am;
            int64_t  bs = sgn ? (byte ? (int64_t)(int8_t)bm
                                      : (int64_t)(int16_t)bm)
                              : (int64_t)bm;
            sv = sub ? (as - bs) : (as + bs);
            uv = sub ? ((uint64_t)am - (uint64_t)bm)
                     : ((uint64_t)am + (uint64_t)bm);

            uint32_t lane;
            if (halve) {
                /* Arithmetic shift for the signed classes, logical for the
                 * unsigned ones; the halving forms never saturate. */
                lane = sgn ? (uint32_t)(int32_t)(sv >> 1)
                           : (uint32_t)((sub ? (int64_t)am - (int64_t)bm
                                             : (int64_t)am + (int64_t)bm) >> 1);
            } else if (sat) {
                if (sgn) {
                    int64_t lo = byte ? -128 : -32768;
                    int64_t hi = byte ?  127 :  32767;
                    if (sv < lo) sv = lo;
                    if (sv > hi) sv = hi;
                    lane = (uint32_t)(int32_t)sv;
                } else {
                    int64_t s = sub ? ((int64_t)am - (int64_t)bm)
                                    : ((int64_t)am + (int64_t)bm);
                    int64_t hi = byte ? 255 : 65535;
                    if (s < 0)  s = 0;
                    if (s > hi) s = hi;
                    lane = (uint32_t)s;
                }
            } else {
                lane = (uint32_t)(sgn ? (uint64_t)sv : uv);
            }

            uint32_t mask = byte ? 0xffu : 0xffffu;
            result |= (lane & mask) << sh;

            if (writes_ge) {
                /* Signed: the lane result is non-negative. Unsigned: an add
                 * carried out, or a subtract did not borrow. */
                bool set = sgn ? (sv >= 0)
                               : (sub ? (am >= bm)
                                      : (uv > (uint64_t)mask));
                if (byte) {
                    if (set) ge |= 1u << i;
                } else if (set) {
                    ge |= 3u << (i * 2u);
                }
            }
        }

        c->r[rd] = result;
        if (writes_ge)
            c->cpsr = (c->cpsr & ~(0xfu << 16)) | ((ge & 0xfu) << 16);
        return ARM_OK;
    }

    /*
     * SDIV / UDIV -- ARMv7 only, and genuinely absent on the ARM1176.
     *
     *   SDIV: cond 0111 0001 Rd 1111 Rm 0001 Rn   (0x0710f010)
     *   UDIV: cond 0111 0011 Rd 1111 Rm 0001 Rn   (0x0730f010)
     *
     * Note the operand placement differs from the usual data-processing shape:
     * Rd is at 19:16, Rm at 11:8 and Rn at 3:0, with 15:12 fixed to 1111.
     *
     * The gate is the point of this block. iPhone OS 3 code must keep taking an
     * undefined-instruction trap here, because that trap is what the harness
     * reports as a coverage gap; silently executing a division the real ARM1176
     * would have refused would hide a decode bug on the current target in order
     * to serve a future one.
     */
    /* Bit 21 selects unsigned, so it must be OUTSIDE the mask; bits 23 and 22
     * are zero in both encodings and stay inside it. */
    if ((insn & 0x0fd0f0f0u) == 0x0710f010u) {
        unsigned rd = (insn >> 16) & 0xfu;
        unsigned rm = (insn >> 8) & 0xfu;
        unsigned rn = insn & 0xfu;
        bool is_unsigned = (insn >> 21) & 1u;
        uint32_t n, m;

        if (c->arch < ARM_ARCH_V7_SWIFT) return ARM_UNDEFINED;
        /* ARM ARM DDI0406C A8-352/A8-868: any of Rd, Rn, Rm == PC is
         * UNPREDICTABLE. Refuse rather than read the pipelined PC. */
        if (rd == 15u || rn == 15u || rm == 15u) return ARM_UNDEFINED;

        n = c->r[rn];
        m = c->r[rm];
        if (m == 0u) {
            /* Division by zero yields zero when SCTLR.DZ is clear, which is
             * how iOS runs. It is not a trap and not UNPREDICTABLE. */
            c->r[rd] = 0u;
        } else if (is_unsigned) {
            c->r[rd] = n / m;
        } else if (n == UINT32_C(0x80000000) && m == UINT32_C(0xffffffff)) {
            /* INT_MIN / -1 overflows a signed 32-bit result; the architecture
             * defines the answer as INT_MIN rather than trapping. Computing it
             * in C signed arithmetic would be undefined behaviour. */
            c->r[rd] = UINT32_C(0x80000000);
        } else {
            c->r[rd] = (uint32_t)((int32_t)n / (int32_t)m);
        }
        return ARM_OK;
    }

    /*
     * PKHBT and PKHTB — pack halfword.
     *
     *   cccc 0110 1000 nnnn dddd iiiii t01 mmmm      t: 0 = BT, 1 = TB
     *
     * WHY THIS IS HERE: r205 stopped on `PKHTB r4, r4, r3, ASR #16`
     * (0xe6844853) at 0x33922c78 in pid 38, roughly 340 M instructions after a
     * tap that opened Notes. Nothing before it had ever executed a media
     * instruction from this family, so the gap survived every earlier boot; it
     * is reached the moment userspace runs halfword-packing code.
     *
     * BT takes the BOTTOM halfword of Rn and the TOP of the shifted Rm; TB is
     * the reverse. The shift applies to Rm only, LSL for BT and ASR for TB, and
     * for TB an imm5 of zero encodes ASR #32 rather than no shift — an
     * arithmetic shift of 32 leaves every bit equal to the sign bit, which is
     * why it is computed rather than shifted (a C shift by 32 on a 32-bit type
     * is undefined behaviour).
     *
     * The halves are combined AFTER shifting, so nothing can bleed between them.
     */
    if ((insn & 0x0ff00030u) == 0x06800010u) {
        unsigned rn = (insn >> 16) & 0xfu;
        unsigned rd = (insn >> 12) & 0xfu;
        unsigned rm = insn & 0xfu;
        unsigned imm5 = (insn >> 7) & 0x1fu;
        bool tb = ((insn >> 6) & 1u) != 0u;
        if (rd == 15u || rn == 15u || rm == 15u) return ARM_UNDEFINED;

        uint32_t n = reg_read(c, pc, rn);
        uint32_t m = reg_read(c, pc, rm);
        uint32_t shifted;
        if (tb) {
            /* ASR, with imm5 == 0 meaning #32: every bit becomes the sign. */
            shifted = imm5 ? (uint32_t)((int32_t)m >> imm5)
                           : (uint32_t)((int32_t)m >> 31);
            c->r[rd] = (n & 0xffff0000u) | (shifted & 0x0000ffffu);
        } else {
            shifted = imm5 ? (m << imm5) : m;
            c->r[rd] = (n & 0x0000ffffu) | (shifted & 0xffff0000u);
        }
        return ARM_OK;
    }

    /*
     * SEL — select bytes using the GE flags.
     *
     *   cccc 0110 1000 nnnn dddd 1111 1011 mmmm
     *
     * Implemented alongside PKH rather than after the next stop, for the reason
     * the parallel add/sub family above states: SEL is what READS the GE bits
     * those instructions write, so the producer being present and the consumer
     * trapping is a gap of exactly one instruction in the middle of working
     * code. Each GE bit picks one byte lane from Rn, otherwise from Rm.
     */
    if ((insn & 0x0ff00ff0u) == 0x06800fb0u) {
        unsigned rn = (insn >> 16) & 0xfu;
        unsigned rd = (insn >> 12) & 0xfu;
        unsigned rm = insn & 0xfu;
        if (rd == 15u || rn == 15u || rm == 15u) return ARM_UNDEFINED;
        uint32_t n = reg_read(c, pc, rn), m = reg_read(c, pc, rm);
        uint32_t ge = (c->cpsr >> 16) & 0xfu;
        uint32_t out = 0u;
        for (unsigned lane = 0; lane < 4u; lane++) {
            uint32_t mask = UINT32_C(0xff) << (lane * 8u);
            out |= ((ge >> lane) & 1u) ? (n & mask) : (m & mask);
        }
        c->r[rd] = out;
        return ARM_OK;
    }

    /*
     * ARMv6 signed dual multiplies, and the rest of the multiply-accumulate
     * media space with them.
     *
     * r207 stopped on `SMLAD r8, r2, lr, r8` (0xe7088e12) at 0x33932dfc, one
     * billion instructions FURTHER than r205 reached -- so implementing PKH
     * bought real progress and then landed on the next member of the same
     * unimplemented space. Each of these costs a forty-minute run to discover
     * one at a time, which is precisely the argument the parallel add/sub
     * family above already makes for implementing a family together: the
     * neighbours differ only in how the lane products are reduced, so leaving
     * them to trap hides the next stop behind an identical shape.
     *
     * Four encodings, all with bit 4 set in the 011 space:
     *
     *   0x70  SMLAD SMUAD SMLSD SMUSD   dual 16x16, added or subtracted
     *   0x74  SMLALD SMLSLD             the same, accumulating into 64 bits
     *   0x75  SMMUL SMMLA SMMLS         32x32 keeping the TOP word
     *   0x78  USAD8 USADA8              sum of absolute byte differences
     *
     * Ra == 15 is what distinguishes the accumulating form from the plain one
     * (SMLAD/SMUAD, SMMLA/SMMUL, USADA8/USAD8); it is an encoding field, not a
     * register read, so it is tested before reading anything.
     *
     * Products are formed in int64 and narrowed once, so a lane can never carry
     * into its neighbour, and Q is set from whether the full-precision result
     * fitted -- sticky, never cleared here.
     */
    if ((insn & 0x0ff00090u) == 0x07000010u) {        /* SMLAD SMUAD SMLSD SMUSD */
        unsigned rd = (insn >> 16) & 0xfu, ra = (insn >> 12) & 0xfu;
        unsigned rm = (insn >> 8) & 0xfu, rn = insn & 0xfu;
        bool sub  = ((insn >> 6) & 1u) != 0u;
        bool swap = ((insn >> 5) & 1u) != 0u;
        if (rd == 15u || rm == 15u || rn == 15u) return ARM_UNDEFINED;
        uint32_t n = reg_read(c, pc, rn), m = reg_read(c, pc, rm);
        if (swap) m = (m >> 16) | (m << 16);          /* the X forms */
        int64_t p1 = (int64_t)(int16_t)(uint16_t)(n & 0xffffu) *
                     (int16_t)(uint16_t)(m & 0xffffu);
        int64_t p2 = (int64_t)(int16_t)(uint16_t)(n >> 16) *
                     (int16_t)(uint16_t)(m >> 16);
        int64_t acc = sub ? (p1 - p2) : (p1 + p2);
        if (ra != 15u) acc += (int64_t)(int32_t)reg_read(c, pc, ra);
        if (acc != (int64_t)(int32_t)acc) c->cpsr |= ARM_CPSR_Q;
        c->r[rd] = (uint32_t)((uint64_t)acc & UINT64_C(0xffffffff));
        return ARM_OK;
    }
    if ((insn & 0x0ff00090u) == 0x07400010u) {        /* SMLALD SMLSLD */
        unsigned rdhi = (insn >> 16) & 0xfu, rdlo = (insn >> 12) & 0xfu;
        unsigned rm = (insn >> 8) & 0xfu, rn = insn & 0xfu;
        bool sub  = ((insn >> 6) & 1u) != 0u;
        bool swap = ((insn >> 5) & 1u) != 0u;
        if (rdhi == 15u || rdlo == 15u || rm == 15u || rn == 15u ||
            rdhi == rdlo)
            return ARM_UNDEFINED;
        uint32_t n = reg_read(c, pc, rn), m = reg_read(c, pc, rm);
        if (swap) m = (m >> 16) | (m << 16);
        int64_t p1 = (int64_t)(int16_t)(uint16_t)(n & 0xffffu) *
                     (int16_t)(uint16_t)(m & 0xffffu);
        int64_t p2 = (int64_t)(int16_t)(uint16_t)(n >> 16) *
                     (int16_t)(uint16_t)(m >> 16);
        uint64_t wide = ((uint64_t)reg_read(c, pc, rdhi) << 32) |
                        reg_read(c, pc, rdlo);
        int64_t acc = (int64_t)wide + (sub ? (p1 - p2) : (p1 + p2));
        c->r[rdlo] = (uint32_t)((uint64_t)acc & UINT64_C(0xffffffff));
        c->r[rdhi] = (uint32_t)((uint64_t)acc >> 32);
        return ARM_OK;
    }
    if ((insn & 0x0ff00010u) == 0x07500010u) {        /* SMMUL SMMLA SMMLS */
        unsigned sel = (insn >> 6) & 3u;              /* 00 add, 11 subtract */
        if (sel != 0u && sel != 3u) return ARM_UNDEFINED;
        unsigned rd = (insn >> 16) & 0xfu, ra = (insn >> 12) & 0xfu;
        unsigned rm = (insn >> 8) & 0xfu, rn = insn & 0xfu;
        bool sub = (sel == 3u), round = ((insn >> 5) & 1u) != 0u;
        if (rd == 15u || rm == 15u || rn == 15u) return ARM_UNDEFINED;
        /* SMMUL has no accumulator and is the Ra == 15 encoding; SMMLS with
         * Ra == 15 is not an encoding at all. */
        if (sub && ra == 15u) return ARM_UNDEFINED;
        int64_t prod = (int64_t)(int32_t)reg_read(c, pc, rn) *
                       (int32_t)reg_read(c, pc, rm);
        int64_t acc = (ra == 15u)
            ? 0 : ((int64_t)(int32_t)reg_read(c, pc, ra) << 32);
        int64_t res = sub ? (acc - prod) : (acc + prod);
        if (round) res += INT64_C(0x80000000);
        c->r[rd] = (uint32_t)((uint64_t)res >> 32);
        return ARM_OK;
    }
    if ((insn & 0x0ff000f0u) == 0x07800010u) {        /* USAD8 USADA8 */
        unsigned rd = (insn >> 16) & 0xfu, ra = (insn >> 12) & 0xfu;
        unsigned rm = (insn >> 8) & 0xfu, rn = insn & 0xfu;
        if (rd == 15u || rm == 15u || rn == 15u) return ARM_UNDEFINED;
        uint32_t n = reg_read(c, pc, rn), m = reg_read(c, pc, rm);
        uint32_t sum = (ra == 15u) ? 0u : reg_read(c, pc, ra);
        for (unsigned lane = 0; lane < 4u; lane++) {
            uint32_t a = (n >> (lane * 8u)) & 0xffu;
            uint32_t b = (m >> (lane * 8u)) & 0xffu;
            sum += (a > b) ? (a - b) : (b - a);   /* unsigned, so no negatives */
        }
        c->r[rd] = sum;
        return ARM_OK;
    }

    /*
     * SSAT / USAT -- signed input clamped to a signed or unsigned range.
     *
     *   cccc 0110 1u1 sssss dddd iiiii t01 nnnn
     *
     * Safari's pages view and SpringBoard's Spotlight transition independently
     * stopped at 0x3393d544 and 0x33932e54 on the same instruction word,
     * `USAT r3, #8, r6, ASR #14` (0xe6e83756). This was not a scheduler pause:
     * the media decoder deliberately reached its unsupported catch-all below.
     *
     * The source is shifted as a 32-bit value before saturation. For ASR an
     * imm5 of zero means #32; barrel_shift already implements that distinction.
     * SSAT encodes widths 1..32 as field+1, while USAT encodes 0..31 directly.
     * Saturation sets Q and a non-saturating operation leaves the sticky flag
     * alone. NZCV and GE are never changed.
     */
    if ((insn & 0x0fa00030u) == 0x06a00010u) {
        bool uns = ((insn >> 22) & 1u) != 0u;
        unsigned sat_field = (insn >> 16) & 0x1fu;
        unsigned rd = (insn >> 12) & 0xfu;
        unsigned imm5 = (insn >> 7) & 0x1fu;
        bool asr = ((insn >> 6) & 1u) != 0u;
        unsigned rn = insn & 0xfu;
        bool ignored_carry = false, saturated = false;
        uint32_t shifted;
        int64_t value;

        if (rd == 15u || rn == 15u) return ARM_UNDEFINED;
        shifted = barrel_shift(reg_read(c, pc, rn), asr ? 2u : 0u,
                               imm5, false, &ignored_carry);
        value = (shifted & UINT32_C(0x80000000))
              ? (int64_t)shifted - INT64_C(0x100000000)
              : (int64_t)shifted;
        c->r[rd] = uns
            ? unsigned_saturate(value, sat_field, &saturated)
            : signed_saturate(value, sat_field + 1u, &saturated);
        if (saturated) c->cpsr |= ARM_CPSR_Q;
        return ARM_OK;
    }

    /* SSAT16 / USAT16 apply the same rules independently to the two signed
     * halfwords in Rn. Their width fields are 1..16 (signed, field+1) and
     * 0..15 (unsigned, direct). Either lane saturating sets the shared Q bit. */
    if ((insn & 0x0fb00ff0u) == 0x06a00f30u) {
        bool uns = ((insn >> 22) & 1u) != 0u;
        unsigned sat_field = (insn >> 16) & 0xfu;
        unsigned rd = (insn >> 12) & 0xfu;
        unsigned rn = insn & 0xfu;
        bool saturated = false;
        uint32_t source, result = 0u;

        if (rd == 15u || rn == 15u) return ARM_UNDEFINED;
        source = reg_read(c, pc, rn);
        for (unsigned lane = 0; lane < 2u; lane++) {
            uint32_t raw = (source >> (lane * 16u)) & 0xffffu;
            int64_t value = (raw & 0x8000u)
                ? (int64_t)raw - INT64_C(0x10000) : (int64_t)raw;
            uint32_t clamped = uns
                ? unsigned_saturate(value, sat_field, &saturated)
                : signed_saturate(value, sat_field + 1u, &saturated);
            result |= (clamped & 0xffffu) << (lane * 16u);
        }
        c->r[rd] = result;
        if (saturated) c->cpsr |= ARM_CPSR_Q;
        return ARM_OK;
    }

    return ARM_UNDEFINED;                  /* remaining unimplemented media */
}

static arm_status_t exec_multiply(arm_cpu_t *c, uint32_t pc, uint32_t insn) {
    bool A  = (insn >> 21) & 1u;  /* accumulate (MLA) */
    bool S  = (insn >> 20) & 1u;
    unsigned rd = (insn >> 16) & 0xf; /* note: Rd/Rn fields are swapped vs data-proc */
    unsigned rn = (insn >> 12) & 0xf;
    unsigned rs = (insn >> 8)  & 0xf;
    unsigned rm = insn & 0xf;
    uint32_t res = reg_read(c, pc, rm) * reg_read(c, pc, rs);
    if (A) res += reg_read(c, pc, rn);
    c->r[rd] = res;
    if (S) {
        set_flag(c, ARM_CPSR_N, (res >> 31) & 1u);
        set_flag(c, ARM_CPSR_Z, res == 0);
        /* C is unpredictable after MUL on ARMv6; leave as-is. V preserved. */
    }
    return ARM_OK;
}

/*
 * The 64-bit multiplies: UMULL, UMLAL, SMULL, SMLAL.
 *
 *   cccc 0000 1 Un A S hhhh llll ssss 1001 mmmm
 *
 * Bit 22 SELECTS SIGNED, not unsigned: 100=UMULL, 101=UMLAL, 110=SMULL,
 * 111=SMLAL for bits[23:21]. Getting that polarity backwards is invisible for
 * small operands and wrong only in the high word, which is exactly the sort of
 * error that shows up a long way from its cause. A accumulates into the existing
 * RdHi:RdLo. These are ordinary compiled code — a driver in the real 3.1.3
 * kernelcache stopped the boot dead on a plain UMULL — so the distinction
 * between signed and unsigned matters: doing the multiply in 64-bit unsigned
 * for both gives the right low word and a silently wrong high word.
 */
static arm_status_t exec_multiply_long(arm_cpu_t *c, uint32_t pc, uint32_t insn) {
    bool is_signed = (insn >> 22) & 1u;
    bool A = (insn >> 21) & 1u;
    bool S = (insn >> 20) & 1u;
    unsigned rdhi = (insn >> 16) & 0xfu;
    unsigned rdlo = (insn >> 12) & 0xfu;
    unsigned rs   = (insn >> 8)  & 0xfu;
    unsigned rm   = insn & 0xfu;

    /* PC as an operand or destination here is unpredictable, and RdHi must
     * differ from RdLo. Trap rather than invent a result. */
    if (rdhi == 15 || rdlo == 15 || rs == 15 || rm == 15 || rdhi == rdlo)
        return ARM_UNDEFINED;

    uint32_t a = reg_read(c, pc, rm), b = reg_read(c, pc, rs);
    uint64_t res;
    if (is_signed) {
        /* Sign-extend through int64 so the high word is right. */
        res = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
    } else {
        res = (uint64_t)a * (uint64_t)b;
    }
    if (A) res += ((uint64_t)c->r[rdhi] << 32) | (uint64_t)c->r[rdlo];

    c->r[rdlo] = (uint32_t)res;
    c->r[rdhi] = (uint32_t)(res >> 32);
    if (S) {
        set_flag(c, ARM_CPSR_N, (res >> 63) & 1u);
        set_flag(c, ARM_CPSR_Z, res == 0);
        /* C and V are unpredictable after a long multiply on ARMv6. */
    }
    return ARM_OK;
}

/*
 * ARMv5TE signed halfword multiplies (ARM DDI 0100I A4.1.74, A4.1.77,
 * A4.1.79, A4.1.86, and A4.1.88).  These sit in the ARM data-processing
 * encoding space, so they must be decoded before the generic ALU path.
 *
 * Keep the conversions explicit instead of relying on implementation-defined
 * unsigned-to-signed casts or right-shifting a negative C value.  That matters
 * here because the portable core is also built by Apple's Clang for the app.
 */
static int64_t dsp_signed_half(uint32_t value, bool top) {
    uint32_t half = (value >> (top ? 16u : 0u)) & 0xffffu;
    return (half & 0x8000u) ? (int64_t)half - INT64_C(65536)
                            : (int64_t)half;
}

static int64_t dsp_signed_word(uint32_t value) {
    return (value & 0x80000000u) ? (int64_t)value - INT64_C(4294967296)
                                 : (int64_t)value;
}

static uint32_t dsp_accumulate_word(arm_cpu_t *c, int64_t product,
                                    uint32_t accumulator) {
    int64_t sum = product + dsp_signed_word(accumulator);
    if (sum > INT32_MAX || sum < INT32_MIN)
        c->cpsr |= ARM_CPSR_Q;       /* Q is sticky; these instructions never clear it */
    return (uint32_t)sum;            /* architected modulo-2^32 result */
}

static arm_status_t exec_dsp_multiply(arm_cpu_t *c, uint32_t insn) {
    unsigned rd = (insn >> 16) & 0xfu;
    unsigned rn = (insn >> 12) & 0xfu;
    unsigned rs = (insn >> 8)  & 0xfu;
    unsigned rm = insn & 0xfu;
    bool x = (insn & (1u << 5)) != 0u;  /* top half of Rm */
    bool y = (insn & (1u << 6)) != 0u;  /* top half of Rs */

    /* SMLAxy Rd,Rm,Rs,Rn: signed halfword product plus signed word. */
    if ((insn & 0x0ff00090u) == 0x01000080u) {
        if (rd == 15u || rn == 15u || rs == 15u || rm == 15u)
            return ARM_UNDEFINED;
        int64_t product = dsp_signed_half(c->r[rm], x)
                        * dsp_signed_half(c->r[rs], y);
        c->r[rd] = dsp_accumulate_word(c, product, c->r[rn]);
        return ARM_OK;
    }

    /* SMLAWy Rd,Rm,Rs,Rn: bits[47:16] of word*halfword, then accumulate. */
    if ((insn & 0x0ff000b0u) == 0x01200080u) {
        if (rd == 15u || rn == 15u || rs == 15u || rm == 15u)
            return ARM_UNDEFINED;
        int64_t product = dsp_signed_word(c->r[rm])
                        * dsp_signed_half(c->r[rs], y);
        uint32_t upper = (uint32_t)((uint64_t)product >> 16);
        c->r[rd] = dsp_accumulate_word(c, dsp_signed_word(upper), c->r[rn]);
        return ARM_OK;
    }

    /* SMULWy Rd,Rm,Rs: the same word*halfword product, without accumulate. */
    if ((insn & 0x0ff0f0b0u) == 0x012000a0u) {
        if (rd == 15u || rs == 15u || rm == 15u)
            return ARM_UNDEFINED;
        int64_t product = dsp_signed_word(c->r[rm])
                        * dsp_signed_half(c->r[rs], y);
        c->r[rd] = (uint32_t)((uint64_t)product >> 16);
        return ARM_OK;
    }

    /* SMLALxy RdLo,RdHi,Rm,Rs: signed product added modulo 2^64. */
    if ((insn & 0x0ff00090u) == 0x01400080u) {
        unsigned rdhi = rd, rdlo = rn;
        if (rdhi == 15u || rdlo == 15u || rs == 15u || rm == 15u ||
            rdhi == rdlo)
            return ARM_UNDEFINED;

        /* Read the complete accumulator and both sources before either write:
         * ARM1176 permits a source to alias a destination. */
        uint64_t accumulator = ((uint64_t)c->r[rdhi] << 32) | c->r[rdlo];
        int64_t product = dsp_signed_half(c->r[rm], x)
                        * dsp_signed_half(c->r[rs], y);
        uint64_t result = accumulator + (uint64_t)product;
        c->r[rdlo] = (uint32_t)result;
        c->r[rdhi] = (uint32_t)(result >> 32);
        return ARM_OK;
    }

    /* SMULxy Rd,Rm,Rs: exact signed 16x16 -> signed 32-bit product. */
    if ((insn & 0x0ff0f090u) == 0x01600080u) {
        if (rd == 15u || rs == 15u || rm == 15u)
            return ARM_UNDEFINED;
        int64_t product = dsp_signed_half(c->r[rm], x)
                        * dsp_signed_half(c->r[rs], y);
        c->r[rd] = (uint32_t)product;
        return ARM_OK;
    }

    /* Includes SMUL/SMULW encodings whose SBZ field is non-zero. */
    return ARM_UNDEFINED;
}

/* ------------------------------------------------------------------- step */

/* ==================================================================== Thumb */
/*
 * The Thumb (16-bit) instruction set. iPhone OS userland — SpringBoard and
 * most of the frameworks — is Thumb-compiled for code density, so this is not
 * optional for reaching a home screen.
 *
 * Thumb reuses every helper above (barrel shifter, ALU flag logic, translated
 * memory accessors, exception entry), so semantics cannot drift between the two
 * instruction sets. Reading r15 in Thumb yields the instruction address + 4,
 * and with bit 1 cleared for PC-relative loads.
 */
#define TB(n)  ((insn >> (n)) & 7u)          /* 3-bit register field at bit n */

static arm_status_t thumb_step(arm_cpu_t *c, uint32_t pc, uint16_t insn,
                               uint32_t *next) {
    const uint32_t pc4 = pc + 4;             /* r15 as read by the instruction */

    switch (insn >> 12) {
    case 0x0: case 0x1: {
        if ((insn & 0xf800u) == 0x1800u) {   /* ADD/SUB register or 3-bit imm */
            unsigned rd = TB(0), rs = TB(3);
            uint32_t op2 = (insn & (1u << 10)) ? (uint32_t)TB(6) : c->r[TB(6)];
            c->r[rd] = (insn & (1u << 9)) ? alu_sub(c, c->r[rs], op2, 1, true)
                                          : alu_add(c, c->r[rs], op2, 0, true);
            return ARM_OK;
        }
        /* LSL/LSR/ASR by immediate */
        unsigned rd = TB(0), rs = TB(3), amount = (insn >> 6) & 0x1fu;
        unsigned type = (insn >> 11) & 3u;
        bool carry = get_flag(c, ARM_CPSR_C);
        uint32_t res = barrel_shift(c->r[rs], type, amount, false, &carry);
        c->r[rd] = res;
        alu_logic_flags(c, res, carry, true);
        return ARM_OK;
    }
    case 0x2: case 0x3: {                    /* MOV/CMP/ADD/SUB 8-bit immediate */
        unsigned rd = (insn >> 8) & 7u;
        uint32_t imm = insn & 0xffu;
        switch ((insn >> 11) & 3u) {
            case 0: c->r[rd] = imm; alu_logic_flags(c, imm, get_flag(c, ARM_CPSR_C), true); break;
            case 1: alu_sub(c, c->r[rd], imm, 1, true); break;              /* CMP */
            case 2: c->r[rd] = alu_add(c, c->r[rd], imm, 0, true); break;
            default: c->r[rd] = alu_sub(c, c->r[rd], imm, 1, true); break;
        }
        return ARM_OK;
    }
    case 0x4: {
        if ((insn & 0xfc00u) == 0x4000u) {   /* ALU operations */
            unsigned rd = TB(0), rs = TB(3);
            uint32_t a = c->r[rd], b = c->r[rs];
            bool carry = get_flag(c, ARM_CPSR_C);
            switch ((insn >> 6) & 0xfu) {
                case 0x0: c->r[rd] = a & b; alu_logic_flags(c, c->r[rd], carry, true); break; /* AND */
                case 0x1: c->r[rd] = a ^ b; alu_logic_flags(c, c->r[rd], carry, true); break; /* EOR */
                case 0x2: c->r[rd] = barrel_shift(a, 0, b & 0xffu, true, &carry);
                          alu_logic_flags(c, c->r[rd], carry, true); break;                   /* LSL */
                case 0x3: c->r[rd] = barrel_shift(a, 1, b & 0xffu, true, &carry);
                          alu_logic_flags(c, c->r[rd], carry, true); break;                   /* LSR */
                case 0x4: c->r[rd] = barrel_shift(a, 2, b & 0xffu, true, &carry);
                          alu_logic_flags(c, c->r[rd], carry, true); break;                   /* ASR */
                case 0x5: c->r[rd] = alu_add(c, a, b, get_flag(c, ARM_CPSR_C), true); break;  /* ADC */
                case 0x6: c->r[rd] = alu_sub(c, a, b, get_flag(c, ARM_CPSR_C), true); break;  /* SBC */
                case 0x7: c->r[rd] = barrel_shift(a, 3, b & 0xffu, true, &carry);
                          alu_logic_flags(c, c->r[rd], carry, true); break;                   /* ROR */
                case 0x8: alu_logic_flags(c, a & b, carry, true); break;                      /* TST */
                case 0x9: c->r[rd] = alu_sub(c, 0, b, 1, true); break;                        /* NEG */
                case 0xa: alu_sub(c, a, b, 1, true); break;                                   /* CMP */
                case 0xb: alu_add(c, a, b, 0, true); break;                                   /* CMN */
                case 0xc: c->r[rd] = a | b; alu_logic_flags(c, c->r[rd], carry, true); break; /* ORR */
                case 0xd: c->r[rd] = a * b;
                          set_flag(c, ARM_CPSR_N, (c->r[rd] >> 31) & 1u);
                          set_flag(c, ARM_CPSR_Z, c->r[rd] == 0); break;                      /* MUL */
                case 0xe: c->r[rd] = a & ~b; alu_logic_flags(c, c->r[rd], carry, true); break;/* BIC */
                default:  c->r[rd] = ~b; alu_logic_flags(c, c->r[rd], carry, true); break;    /* MVN */
            }
            return ARM_OK;
        }
        if ((insn & 0xfc00u) == 0x4400u) {   /* Hi-register ops and BX/BLX */
            unsigned rd = (unsigned)TB(0) | ((insn >> 4) & 8u);
            unsigned rs = (unsigned)TB(3) | ((insn >> 3) & 8u);
            uint32_t sv = (rs == 15) ? pc4 : c->r[rs];
            switch ((insn >> 8) & 3u) {
                case 0:                                        /* ADD */
                    /* Reading R15 in Thumb state yields the instruction's
                     * address + 4 (A6.1.2), which is what pc4 holds. r[15] is
                     * still the raw instruction address here — arm_step does
                     * not write it until after this returns — so using it
                     * branches four bytes short. The sibling CMP and MOV cases
                     * below already use pc4/sv correctly; this one did not. */
                    if (rd == 15) *next = ((pc4 + sv) & ~1u);
                    else c->r[rd] += sv;
                    return ARM_OK;
                case 1: alu_sub(c, (rd == 15) ? pc4 : c->r[rd], sv, 1, true); return ARM_OK; /* CMP */
                case 2:                                        /* MOV */
                    if (rd == 15) *next = sv & ~1u;
                    else c->r[rd] = sv;
                    return ARM_OK;
                default:                                       /* BX / BLX */
                    if ((insn & (1u << 7)) && rs == 15u)
                        return ARM_UNDEFINED;                    /* BLX pc */
                    if ((sv & 3u) == 2u) return ARM_UNDEFINED;
                    if (insn & (1u << 7)) c->r[14] = (pc + 2) | 1u;   /* BLX */
                    if (!(sv & 1u)) c->cpsr &= ~ARM_CPSR_T;           /* back to ARM */
                    *next = sv & ~1u;
                    return ARM_OK;
            }
        }
        /* PC-relative load: LDR Rd,[PC,#imm8*4] — PC is word-aligned first. */
        unsigned rd = (insn >> 8) & 7u;
        uint32_t addr = (pc4 & ~3u) + ((insn & 0xffu) << 2);
        uint32_t v = mem_r32(c, addr);
        if (c->abort_pending) return ARM_OK;
        c->r[rd] = v;
        return ARM_OK;
    }
    case 0x5: {
        unsigned rd = TB(0), rb = TB(3), ro = TB(6);
        uint32_t addr = c->r[rb] + c->r[ro];
        if (insn & (1u << 9)) {              /* sign-extended byte/halfword */
            uint32_t v;
            unsigned op = (insn >> 10) & 3u;
            if (op != 1u && legacy_halfword_unpredictable(c, addr))
                return ARM_UNDEFINED;
            switch (op) {
                case 0:
                    mem_w16(c, addr, (uint16_t)c->r[rd]);            /* STRH  */
                    return ARM_OK;
                case 1:
                    v = (uint32_t)(int32_t)(int8_t)mem_r8(c, addr);  /* LDRSB */
                    break;
                case 2:
                    v = mem_r16(c, addr);                            /* LDRH  */
                    break;
                default:
                    v = (uint32_t)(int32_t)(int16_t)mem_r16(c, addr);/* LDRSH */
                    break;
            }
            if (c->abort_pending) return ARM_OK;
            c->r[rd] = v;
            return ARM_OK;
        }
        bool load = (insn >> 11) & 1u, byte = (insn >> 10) & 1u;
        if (load) {
            uint32_t v = byte ? mem_r8(c, addr) : mem_r32(c, addr);
            if (c->abort_pending) return ARM_OK;
            c->r[rd] = v;
        } else {
            if (byte) mem_w8(c, addr, (uint8_t)c->r[rd]);
            else      mem_w32(c, addr, c->r[rd]);
        }
        return ARM_OK;
    }
    case 0x6: case 0x7: {                    /* load/store word or byte, imm5 */
        unsigned rd = TB(0), rb = TB(3), off = (insn >> 6) & 0x1fu;
        bool byte = (insn >> 12) & 1u, load = (insn >> 11) & 1u;
        uint32_t addr = c->r[rb] + (byte ? off : (off << 2));
        if (load) {
            uint32_t v = byte ? mem_r8(c, addr) : mem_r32(c, addr);
            if (c->abort_pending) return ARM_OK;
            c->r[rd] = v;
        } else {
            if (byte) mem_w8(c, addr, (uint8_t)c->r[rd]);
            else      mem_w32(c, addr, c->r[rd]);
        }
        return ARM_OK;
    }
    case 0x8: {                              /* load/store halfword, imm5 */
        unsigned rd = TB(0), rb = TB(3);
        uint32_t addr = c->r[rb] + (((insn >> 6) & 0x1fu) << 1);
        if (legacy_halfword_unpredictable(c, addr)) return ARM_UNDEFINED;
        if (insn & (1u << 11)) {
            uint32_t v = mem_r16(c, addr);
            if (c->abort_pending) return ARM_OK;
            c->r[rd] = v;
        } else {
            mem_w16(c, addr, (uint16_t)c->r[rd]);
        }
        return ARM_OK;
    }
    case 0x9: {                              /* SP-relative load/store */
        unsigned rd = (insn >> 8) & 7u;
        uint32_t addr = c->r[13] + ((insn & 0xffu) << 2);
        if (insn & (1u << 11)) {
            uint32_t v = mem_r32(c, addr);
            if (c->abort_pending) return ARM_OK;
            c->r[rd] = v;
        } else {
            mem_w32(c, addr, c->r[rd]);
        }
        return ARM_OK;
    }
    case 0xa: {                              /* ADD Rd, PC/SP, #imm8*4 */
        unsigned rd = (insn >> 8) & 7u;
        uint32_t imm = (insn & 0xffu) << 2;
        c->r[rd] = (insn & (1u << 11)) ? c->r[13] + imm : (pc4 & ~3u) + imm;
        return ARM_OK;
    }
    case 0xb: {
        if ((insn & 0xff00u) == 0xb000u) {   /* ADD/SUB SP, #imm7*4 */
            uint32_t imm = (insn & 0x7fu) << 2;
            c->r[13] = (insn & (1u << 7)) ? c->r[13] - imm : c->r[13] + imm;
            return ARM_OK;
        }
        if ((insn & 0xf600u) == 0xb400u) {   /* PUSH / POP */
            uint32_t list = insn & 0xffu;
            bool load = (insn >> 11) & 1u, extra = (insn >> 8) & 1u;
            if (list == 0u && !extra) return ARM_UNDEFINED;
            unsigned n = extra ? 1u : 0u;
            for (unsigned i = 0; i < 8; i++) if (list & (1u << i)) n++;
            if (load) {                       /* POP: ascending from SP */
                uint32_t old_sp = c->r[13], addr = old_sp, loaded[8];
                uint32_t pcv = 0;
                if (!prepare_multiword_address(c, &addr, 4u, false))
                    return ARM_OK;
                for (unsigned i = 0; i < 8; i++) {
                    if (!(list & (1u << i))) continue;
                    loaded[i] = mem_r32(c, addr);
                    if (c->abort_pending) return ARM_OK;
                    addr += 4u;
                }
                if (extra) {
                    pcv = mem_r32(c, addr);
                    if (c->abort_pending) return ARM_OK;
                    if ((pcv & 3u) == 2u) return ARM_UNDEFINED;
                }
                for (unsigned i = 0; i < 8; i++) if (list & (1u << i)) c->r[i] = loaded[i];
                c->r[13] = old_sp + 4u * n;
                if (extra) {                  /* POP {..,pc} may switch to ARM */
                    if (!(pcv & 1u)) c->cpsr &= ~ARM_CPSR_T;
                    *next = pcv & ~1u;
                }
            } else {                          /* PUSH: descending, LR pushed last */
                uint32_t new_sp = c->r[13] - 4u * n, addr = new_sp;
                if (!prepare_multiword_address(c, &addr, 4u, true))
                    return ARM_OK;
                for (unsigned i = 0; i < 8; i++) {
                    if (!(list & (1u << i))) continue;
                    mem_w32(c, addr, c->r[i]);
                    if (c->abort_pending) return ARM_OK;
                    addr += 4;
                }
                if (extra) { mem_w32(c, addr, c->r[14]); if (c->abort_pending) return ARM_OK; }
                c->r[13] = new_sp;
            }
            return ARM_OK;
        }
        /* ARMv6 Thumb extend and byte-reverse group. Real Apple LLB reaches
         * UXTB within a few thousand instructions, so these are not optional. */
        if ((insn & 0xff00u) == 0xb200u) {            /* SXTH/SXTB/UXTH/UXTB */
            unsigned rd = TB(0);
            uint32_t v = c->r[TB(3)];
            switch ((insn >> 6) & 3u) {
                case 0: c->r[rd] = (uint32_t)(int32_t)(int16_t)(uint16_t)v; break; /* SXTH */
                case 1: c->r[rd] = (uint32_t)(int32_t)(int8_t)(uint8_t)v;   break; /* SXTB */
                case 2: c->r[rd] = v & 0xffffu;                            break; /* UXTH */
                default: c->r[rd] = v & 0xffu;                             break; /* UXTB */
            }
            return ARM_OK;
        }
        if ((insn & 0xff00u) == 0xba00u) {            /* REV/REV16/REVSH */
            unsigned rd = TB(0);
            uint32_t v = c->r[TB(3)];
            switch ((insn >> 6) & 3u) {
                case 0:                                                    /* REV */
                    c->r[rd] = ((v & 0xffu) << 24) | ((v & 0xff00u) << 8)
                             | ((v >> 8) & 0xff00u) | ((v >> 24) & 0xffu);
                    break;
                case 1:                                                    /* REV16 */
                    c->r[rd] = ((v & 0x00ffu) << 8)  | ((v & 0xff00u) >> 8)
                             | ((v & 0x00ff0000u) << 8) | ((v & 0xff000000u) >> 8);
                    break;
                case 3: {                                                  /* REVSH */
                    uint16_t h = (uint16_t)(((v & 0xffu) << 8) | ((v >> 8) & 0xffu));
                    c->r[rd] = (uint32_t)(int32_t)(int16_t)h;
                    break;
                }
                default: return ARM_UNDEFINED;
            }
            return ARM_OK;
        }
        /* CPS: 1011 0110 011 im 0 A I F. Like the ARM form it is a no-op in
         * User mode — see the ARM encoding for why that matters. */
        if ((insn & 0xffe0u) == 0xb660u) {
            bool disable = (insn >> 4) & 1u;
            if (!cpu_is_priv(c)) return ARM_OK;
            if (insn & (1u << 2)) set_flag(c, ARM_CPSR_A, disable);
            if (insn & (1u << 1)) set_flag(c, ARM_CPSR_I, disable);
            if (insn & (1u << 0)) set_flag(c, ARM_CPSR_F, disable);
            return ARM_OK;
        }
        /* SETEND, Thumb encoding: 1011 0110 0101 0E00. Same reasoning as the
         * ARM form — LE is a genuine no-op for a little-endian machine, BE
         * keeps trapping because we do not model a big-endian data path and
         * honouring it would corrupt every subsequent load. */
        if ((insn & 0xfff7u) == 0xb650u) {
            if (insn & (1u << 3)) return ARM_UNDEFINED;   /* SETEND BE */
            return ARM_OK;
        }
        return ARM_UNDEFINED;                 /* BKPT and friends: still trap */
    }
    case 0xc: {                              /* STMIA / LDMIA Rb!, {rlist} */
        unsigned rb = (insn >> 8) & 7u;
        uint32_t list = insn & 0xffu, base = c->r[rb], addr = base;
        if (list == 0) return ARM_UNDEFINED;
        unsigned n = 0u;
        for (unsigned i = 0; i < 8; i++) if (list & (1u << i)) n++;
        bool load = (insn & (1u << 11)) != 0u;
        if (!load && (list & (1u << rb)) != 0u) {
            uint32_t lower = rb == 0u ? 0u : ((1u << rb) - 1u);
            if ((list & lower) != 0u) return ARM_UNDEFINED;
        }
        if (!prepare_multiword_address(c, &addr, 4u, !load)) return ARM_OK;
        if (load) {
            uint32_t loaded[8];
            for (unsigned i = 0; i < 8; i++) {
                if (!(list & (1u << i))) continue;
                loaded[i] = mem_r32(c, addr);
                if (c->abort_pending) return ARM_OK;
                addr += 4;
            }
            for (unsigned i = 0; i < 8; i++) if (list & (1u << i)) c->r[i] = loaded[i];
            if (!(list & (1u << rb))) c->r[rb] = base + 4u * n;
        } else {
            for (unsigned i = 0; i < 8; i++) {
                if (!(list & (1u << i))) continue;
                mem_w32(c, addr, c->r[i]);
                if (c->abort_pending) return ARM_OK;
                addr += 4;
            }
            c->r[rb] = base + 4u * n;
        }
        return ARM_OK;
    }
    case 0xd: {
        unsigned cond = (insn >> 8) & 0xfu;
        if (cond == 0xf) {                   /* SWI */
            arm_svc_result_t result =
                privileged_svc_result(c, pc, (uint32_t)insn);
            if (result == ARM_SVC_ERROR) return ARM_HALT;
            if (result == ARM_SVC_REDIRECTED)
                *next = c->r[15];
            else if (result != ARM_SVC_HANDLED)
                take_exception(c, ARM_VEC_SWI, ARM_MODE_SVC, pc + 2, false, next);
            return ARM_OK;
        }
        if (cond == 0xe) return ARM_UNDEFINED;   /* permanently undefined */
        if (arm_cond_passed(c, cond))
            *next = pc4 + ((uint32_t)(int32_t)(int8_t)(insn & 0xffu) << 1);
        return ARM_OK;
    }
    case 0xe: {
        /*
         * 0xE000-0xE7FF is an unconditional branch, but 0xE800-0xEFFF is the
         * SECOND half of a BLX pair, which returns to ARM state. Treating the
         * whole 0xExxx range as a branch sends BLX to a garbage address — real
         * XNU hit this and ended up executing a table of pointers as code.
         */
        if (insn & 0x0800u) {                /* BLX suffix: back to ARM */
            uint32_t target = (c->r[14] + ((uint32_t)(insn & 0x7ffu) << 1)) & ~3u;
            c->r[14] = (pc + 2) | 1u;        /* return address, Thumb bit set */
            c->cpsr &= ~ARM_CPSR_T;
            *next = target;
            return ARM_OK;
        }
        int32_t off = (int32_t)((uint32_t)(insn & 0x7ffu) << 21) >> 20;  /* sign-extend, <<1 */
        *next = pc4 + (uint32_t)off;
        return ARM_OK;
    }
    default: {                               /* 0xf: BL/BLX, a 32-bit pair */
        if (!(insn & (1u << 11))) {          /* first half: LR = PC + (off<<12) */
            int32_t off = (int32_t)((uint32_t)(insn & 0x7ffu) << 21) >> 9;
            c->r[14] = pc4 + (uint32_t)off;
            return ARM_OK;
        }
        uint32_t target = c->r[14] + ((uint32_t)(insn & 0x7ffu) << 1);
        c->r[14] = (pc + 2) | 1u;            /* return address, Thumb bit set */
        *next = target & ~1u;
        return ARM_OK;
    }
    }
}

#undef TB

arm_status_t arm_step(arm_cpu_t *c) {
    uint32_t pc   = c->r[15];

    /* A corrupted snapshot or malformed exception frame must not turn an
     * unimplemented CPSR mode into privileged User-bank execution. */
    if (!arm_mode_is_valid(c->cpsr)) return ARM_UNDEFINED;

    /* Sample the interrupt lines before fetching. FIQ outranks IRQ. The return
     * address convention is "next instruction + 4", so handlers return with
     * SUBS pc, lr, #4. */
    if (c->fiq_line && !(c->cpsr & ARM_CPSR_F)) {
        uint32_t vec;
        c->cycles++;
        take_exception(c, ARM_VEC_FIQ, ARM_MODE_FIQ, pc + 4, true, &vec);
        c->r[15] = vec;
        return ARM_OK;
    }
    if (c->irq_line && !(c->cpsr & ARM_CPSR_I)) {
        uint32_t vec;
        c->cycles++;
        take_exception(c, ARM_VEC_IRQ, ARM_MODE_IRQ, pc + 4, false, &vec);
        c->r[15] = vec;
        return ARM_OK;
    }

    /* Instruction fetch is translated too; a fault here is a prefetch abort.
     * ARM_ACCESS_FETCH rather than "a read": it is what lets the walker check
     * XN, so branching into a data page dies here with IFAR pointing at the
     * branch target instead of executing whatever the data happened to be. */
    uint32_t fetch_pa = 0u;
    /*
     * The fetch-block fast path. See the fetch_* fields in arm.h for why this
     * exists; in short, every instruction pays a translate and an indirect bus
     * call, and inside a 1 KB block both answer the same thing up to 1024
     * times running.
     *
     * A hit requires the same block, the same flush generation and the same
     * privilege -- the three things that can change what a fetch resolves to
     * or whether it is permitted. On a miss the ORIGINAL path runs unchanged,
     * fault behaviour included, and the cache is refilled only if the block is
     * plain RAM.
     */
    const bool fetch_priv = cpu_is_priv(c);
    const uint32_t fetch_blk = pc & ~0x3ffu;
    const uint8_t *fetch_host = NULL;

    if (c->fetch_host && c->fetch_blk == fetch_blk &&
        c->fetch_gen == c->tlb_gen && c->fetch_priv == fetch_priv) {
        fetch_host = c->fetch_host + (pc - fetch_blk);
    } else {
        uint32_t fetch_fsr = arm_mmu_translate(c, pc, ARM_ACCESS_FETCH,
                                               fetch_priv, &fetch_pa);
        if (fetch_fsr) {
            uint32_t vec;
            c->cycles++;
            c->cp15.ifsr = fetch_fsr;
            c->cp15.ifar = pc;
            take_exception(c, ARM_VEC_PREFETCH, ARM_MODE_ABT, pc + 4, false,
                           &vec);
            c->r[15] = vec;
            return ARM_OK;
        }
        if (c->bus->host_ram) {
            uint8_t *blk = c->bus->host_ram(c->bus->ctx, fetch_pa & ~0x3ffu,
                                            0x400u);
            if (blk) {
                c->fetch_host = blk;
                c->fetch_blk  = fetch_blk;
                c->fetch_gen  = c->tlb_gen;
                c->fetch_priv = fetch_priv;
                fetch_host    = blk + (pc - fetch_blk);
            }
        }
    }
    /* Thumb: 16-bit instructions, PC advances by 2. Dispatch before the ARM
     * decoder — the two instruction sets share every helper below. */
    if (c->cpsr & ARM_CPSR_T) {
        /* Assembled from bytes rather than memcpy'd, so it does not depend on
         * the host's endianness; the bus contract is little-endian either
         * way. Instructions are aligned, so a fetch never leaves the block. */
        uint16_t tinsn = fetch_host
            ? (uint16_t)((uint16_t)fetch_host[0] |
                         ((uint16_t)fetch_host[1] << 8))
            : c->bus->read16(c->bus->ctx, fetch_pa);
        uint32_t tnext = pc + 2;
        c->cycles++;
        arm_status_t tst = thumb_step(c, pc, tinsn, &tnext);
        if (tst == ARM_HALT) return ARM_HALT;
        if (c->abort_pending) {
            take_pending_data_abort(c, pc);
            return ARM_OK;
        }
        if (tst == ARM_OK) c->r[15] = tnext;
        return tst;
    }

    uint32_t insn = fetch_host
        ? ((uint32_t)fetch_host[0] | ((uint32_t)fetch_host[1] << 8) |
           ((uint32_t)fetch_host[2] << 16) | ((uint32_t)fetch_host[3] << 24))
        : c->bus->read32(c->bus->ctx, fetch_pa);
    uint32_t next = pc + 4;
    arm_status_t st = ARM_OK;

    c->cycles++;

    uint32_t cond = insn >> 28;

    /* cond==0xF is NOT "always" on ARMv6 — it is the unconditional instruction
     * space (PLD, BLX immediate, SETEND, CPS, RFE, SRS). Decoding it as a
     * normal conditional instruction is catastrophic: PLD would fall into the
     * single-data-transfer decode as "LDRB pc,[Rn]" and branch to whatever byte
     * it loaded. ARMv6-optimised code (memcpy loops in libSystem, XNU copy
     * routines) issues PLD constantly. */
    if (cond == 0xfu) {
        /* PLD is a hint; a no-op is architecturally correct. Both the immediate
         * and register forms have bits[27:26]==01 and the Rd field SBO (1111). */
        /*
         * CLREX must be tested BEFORE PLD. Its encoding 0xF57FF01F satisfies
         * the PLD pattern (bits[27:26]==01 with the Rd field all ones), so
         * with PLD first it is silently treated as a hint and does nothing —
         * which looks harmless and is not: a STREX the architecture requires
         * to fail then succeeds, and two threads can both hold one lock.
         */
        if (insn == 0xf57ff01fu) {
            c->excl_valid = false;
            c->r[15] = next;
            return ARM_OK;
        }
        if ((insn & 0x0c00f000u) == 0x0400f000u) {
            c->r[15] = next;
            return ARM_OK;
        }
        /*
         * CPS — change processor state. Kernels use this constantly to mask
         * interrupts and switch mode in one instruction; real XNU reaches it
         * within the first 40k instructions.
         *   1111 0001 0000 imod M 0 0000 000 A I F 0 mode
         * imod 10 = enable (clear the mask bits), 11 = disable (set them);
         * M selects whether the mode field is applied.
         */
        if ((insn & 0xfff1fe20u) == 0xf1000000u) {
            /*
             * "CPS is a no-op if executed in User mode" (ARM ARM A4.1.16).
             * Unprivileged code can neither unmask an interrupt nor change
             * mode. Executing it regardless is a privilege escalation: a user
             * program running "CPS #0x13" would continue in SVC with its own
             * registers and its own PC, and "CPSIE i" would let it run with
             * interrupts it is not allowed to touch. The kernel-only boot can
             * never expose this — XNU only ever reaches CPS from a privileged
             * mode — so it stays invisible until launchd and dyld run.
             */
            if (!cpu_is_priv(c)) { c->r[15] = next; return ARM_OK; }
            unsigned imod = (insn >> 18) & 3u;
            bool     chg_mode = (insn >> 17) & 1u;
            if (chg_mode && !arm_mode_is_valid(insn)) return ARM_UNDEFINED;
            if (imod & 2u) {
                bool disable = (imod & 1u) != 0;
                if (insn & ARM_CPSR_A) set_flag(c, ARM_CPSR_A, disable);
                if (insn & ARM_CPSR_I) set_flag(c, ARM_CPSR_I, disable);
                if (insn & ARM_CPSR_F) set_flag(c, ARM_CPSR_F, disable);
            }
            if (chg_mode) arm_set_mode(c, insn & ARM_CPSR_MODE_MASK);
            c->r[15] = next;
            return ARM_OK;
        }

        /* BLX <immediate>: an unconditional call that always switches to Thumb.
         *   1111 101 H imm24    target = PC + 8 + (imm24 << 2) + (H << 1) */
        if ((insn & 0xfe000000u) == 0xfa000000u) {
            int32_t off = (int32_t)(insn << 8) >> 6;      /* sign-extend, <<2 */
            uint32_t h = (insn >> 24) & 1u;
            c->r[14] = pc + 4;
            c->cpsr |= ARM_CPSR_T;
            c->r[15] = (pc + 8 + (uint32_t)off + (h << 1)) & ~1u;
            return ARM_OK;
        }

        /*
         * SRS — Store Return State. Pushes the current mode's LR and SPSR onto
         * the banked stack of the mode named in the instruction. XNU uses it on
         * exception entry, paired with RFE to return.
         *   1111 100 P U 1 W 0 1101 0000 0101 000 mode
         */
        if ((insn & 0xfe5fffe0u) == 0xf84d0500u) {
            bool P = (insn >> 24) & 1u, U = (insn >> 23) & 1u, W = (insn >> 21) & 1u;
            uint32_t mode = insn & ARM_CPSR_MODE_MASK;

            /* SRS is undefined in User mode and has no meaningful SPSR in
             * System mode. Treat the latter's architecturally unpredictable
             * case like the other unpredictable encodings this core refuses,
             * rather than fabricating an SPSR from CPSR. */
            uint32_t cur_mode = c->cpsr & ARM_CPSR_MODE_MASK;
            if (cur_mode == ARM_MODE_USR || cur_mode == ARM_MODE_SYS)
                return ARM_UNDEFINED;
            if (!arm_mode_is_valid(mode)) return ARM_UNDEFINED;

            arm_bank_t tb = arm_bank_of_mode(mode);
            arm_bank_t cur = arm_bank_of_mode(c->cpsr);

            /* The target mode's SP: live in r13 if that is the current bank. */
            uint32_t sp = (tb == cur) ? c->r[13] : c->bank_r13[tb];
            uint32_t base = U ? (P ? sp + 4u : sp) : (P ? sp - 8u : sp - 4u);

            if (!prepare_multiword_address(c, &base, 4u, true)) {
                take_pending_data_abort(c, pc);
                return ARM_OK;
            }

            mem_w32(c, base, c->r[14]);
            if (c->abort_pending) { take_pending_data_abort(c, pc); return ARM_OK; }
            mem_w32(c, base + 4u, c->spsr[cur]);
            if (c->abort_pending) { take_pending_data_abort(c, pc); return ARM_OK; }

            if (W) {
                uint32_t wb = U ? sp + 8u : sp - 8u;
                if (tb == cur) c->r[13] = wb; else c->bank_r13[tb] = wb;
            }
            c->r[15] = next;
            return ARM_OK;
        }

        /*
         * RFE — Return From Exception. Loads PC and CPSR from a base register.
         *   1111 100 P U 0 W 1 nnnn 0000 1010 0000 0000
         */
        if ((insn & 0xfe50ffffu) == 0xf8100a00u) {
            bool P = (insn >> 24) & 1u, U = (insn >> 23) & 1u, W = (insn >> 21) & 1u;
            unsigned rn = (insn >> 16) & 0xfu;

            /* RFE writes the complete CPSR, including its mode bits, and is a
             * privileged instruction. Checking before either memory access is
             * essential: a User-mode RFE must not read attacker-selected MMIO
             * before being rejected. */
            if (!cpu_is_priv(c)) return ARM_UNDEFINED;
            if (rn == 15u) return ARM_UNDEFINED;

            uint32_t sp = c->r[rn];
            uint32_t base = U ? (P ? sp + 4u : sp) : (P ? sp - 8u : sp - 4u);

            if (!prepare_multiword_address(c, &base, 4u, false)) {
                take_pending_data_abort(c, pc);
                return ARM_OK;
            }

            uint32_t new_pc = mem_r32(c, base);
            if (c->abort_pending) { take_pending_data_abort(c, pc); return ARM_OK; }
            uint32_t new_cpsr = mem_r32(c, base + 4u);
            if (c->abort_pending) { take_pending_data_abort(c, pc); return ARM_OK; }

            if (!arm_mode_is_valid(new_cpsr)) return ARM_UNDEFINED;
            if ((new_cpsr & ARM_CPSR_T) == 0u && (new_pc & 2u) != 0u)
                return ARM_UNDEFINED;

            if (W) c->r[rn] = U ? sp + 8u : sp - 8u;
            arm_set_mode(c, new_cpsr);
            c->cpsr = (c->cpsr & ARM_CPSR_MODE_MASK) | (new_cpsr & ~ARM_CPSR_MODE_MASK);
            /* Align for the state the restored CPSR selects, not always to a
             * halfword: returning into ARM code must land on a word boundary.
             * Same family of bug as the MOVS pc,lr path — see
             * exec_data_processing. */
            c->r[15] = new_pc & (uint32_t)((c->cpsr & ARM_CPSR_T) ? ~1u : ~3u);
            return ARM_OK;
        }

        /*
         * SETEND — select the data endianness for loads and stores.
         *   1111 0001 0000 0001 0000 0000 E000 0000
         * This core, the bus, and the guest are all little-endian, so SETEND LE
         * is genuinely a no-op and executing it is correct. SETEND BE is not:
         * we do not model a big-endian data path, so honouring it would mean
         * silently doing the wrong thing on every subsequent load. It keeps
         * trapping, which is the honest answer.
         */
        if ((insn & 0xfffffdffu) == 0xf1010000u) {
            if (insn & (1u << 9)) return ARM_UNDEFINED;   /* SETEND BE */
            c->r[15] = next;
            return ARM_OK;
        }
        /* The Advanced SIMD spaces (0xF2/0xF3 data processing, 0xF4 element
         * and structure load/store) live here, in the unconditional space, so
         * the lazy-VFP discrimination has to be applied on this arm too. */
        return undefined_instruction(c, pc, insn);
    }

    /* AL is overwhelmingly the common case and cannot depend on CPSR.  Avoid
     * extracting all four flags and entering the condition switch for it. */
    if (cond != 0xeu && !arm_cond_passed(c, cond)) {
        c->r[15] = next;
        return ARM_OK;
    }

    /* Coarse top-level decode. This intentionally covers only the encodings
     * M1 implements; anything else returns ARM_UNDEFINED so the harness/log
     * can show us exactly which instruction to implement next. */
    /*
     * DATA PROCESSING WITH AN IMMEDIATE, HOISTED, BECAUSE CHAIN DEPTH COSTS.
     *
     * Data processing is the commonest class of ARM instruction there is, and
     * before this it was the LAST arm below -- about twenty-five comparisons
     * deep, where LDR/STR leaves at six. Thumb dispatches through a switch on
     * insn>>12 instead of a chain, and insnbench measured ARM at 0.58 of Thumb
     * on comparable loops. Hoisting this one arm moved that to 0.86: the same
     * loop went 10.95 -> 17.01 M insn/s, about 1.48x, with 54 tests passing.
     *
     * SO DEPTH IS A REAL COST, and that is worth stating carefully because it
     * looks at first like it contradicts a result already recorded in
     * docs/hotpath.md -- "decode scatter is free", measured as a ten-position
     * mixed loop running no slower than a one-position loop. It does not.
     * That experiment varied WHICH arm each instruction leaves by, holding the
     * distances roughly alike; this one changes HOW FAR the commonest arm is.
     * Scatter is free. Depth is not. Both are true.
     *
     * BOTH FORMS ARE HOISTED, immediate and register, because the register
     * form was no better off: it sat behind the whole LDREX/STREX/SWP family,
     * about twelve further comparisons, and `MOV rd, rn, LSL #1` is every bit
     * as common as `ADD rd, rn, #1`.
     *
     * THE GUARD IS EXACT, and it is the ARM decode rule written out rather
     * than an approximation of it. Bits 27-26 == 00 is the data-processing
     * encoding space. Exactly two things live inside it that are not data
     * processing, and each is excluded by name:
     *
     *   - the MISCELLANEOUS space, op1 == 10xx0, i.e. bit24 set with bit23 and
     *     bit20 clear. That is MSR, MRS, BX, BLX(reg), CLZ, the saturating
     *     add/subtracts, the DSP multiplies and BKPT.
     *   - MULTIPLY and EXTRA LOAD/STORE, which are bits 7 and 4 both set, in
     *     the register half only. That is MUL, UMULL, LDRH/STRH/LDRD/STRD,
     *     the exclusives and SWP.
     *
     * Every arm between here and the original catch-all that could match bits
     * 27-25 == 000 was checked to require (insn & 0x90) == 0x90, so none of
     * them can be swallowed. Anything this declines reaches its original arm
     * unchanged, which is what makes the hoist a reordering rather than a
     * reinterpretation.
     *
     * This is a first cut at the shape, not the end of it. The rest of the
     * chain is still linear, and the principled fix is to dispatch on bits
     * 27-25 the way the Thumb decoder dispatches on insn>>12.
     */
    if ((insn & 0x0c000000u) == 0x00000000u &&        /* the DP encoding space */
        (insn & 0x01900000u) != 0x01000000u &&        /* not misc: MSR/BX/CLZ/DSP */
        !((insn & 0x0e000000u) == 0x00000000u &&
          (insn & 0x00000090u) == 0x00000090u)) {     /* not multiply/extra-ls */
        st = exec_data_processing(c, pc, insn, &next);
    } else if ((insn & 0x0f000000u) == 0x0a000000u ||
        (insn & 0x0f000000u) == 0x0b000000u) {           /* B / BL */
        st = exec_branch(c, pc, insn, &next);
    } else if ((insn & 0x0fc000f0u) == 0x00000090u) {     /* MUL / MLA */
        st = exec_multiply(c, pc, insn);
    } else if ((insn & 0x0f8000f0u) == 0x00800090u) {     /* UMULL/UMLAL/SMULL/SMLAL */
        st = exec_multiply_long(c, pc, insn);
    } else if ((insn & 0x0f900090u) == 0x01000080u) {     /* ARMv5TE DSP multiplies */
        st = exec_dsp_multiply(c, insn);
    } else if ((insn & 0x0e000000u) == 0x06000000u &&
               (insn & 0x00000010u) != 0) {
        /* bits[27:25]==011 with bit4==1 is the ARMv6 media space. The extend
         * and byte-reverse families are implemented; the rest still trap, so
         * they are named rather than silently mis-executed as loads/stores. */
        st = exec_media(c, pc, insn);
    } else if ((insn & 0x0c000000u) == 0x04000000u) {     /* single data transfer */
        st = exec_single_transfer(c, pc, insn, &next);
    } else if ((insn & 0x0e000000u) == 0x08000000u) {     /* LDM / STM */
        st = exec_block_transfer(c, pc, insn, &next);
    } else if ((insn & 0x0e000000u) == 0x00000000u &&
               (insn & 0x00000090u) == 0x00000090u &&
               (insn & 0x00000060u) != 0x00000000u) {
        /* bits[6:5] != 00 -> extra load/store (LDRH/STRH/LDRSB/LDRSH, LDRD/STRD) */
        st = exec_extra_transfer(c, pc, insn, &next);
    } else if ((insn & 0x0ff00ff0u) == 0x01900f90u) {     /* LDREX Rd,[Rn] */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        if (rn == 15u || rd == 15u) { st = ARM_UNDEFINED; }
        else {
            uint32_t addr = c->r[rn];
            if (prepare_sync_address(c, addr, 4u, false, &st)) {
                uint32_t v = mem_r32(c, addr);
                if (!c->abort_pending) {
                    c->r[rd] = v;
                    c->excl_valid = true;      /* tag the address as exclusive */
                    c->excl_addr  = addr;
                }
            }
        }
    } else if ((insn & 0x0ff00ff0u) == 0x01800f90u) {     /* STREX Rd,Rm,[Rn] */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        unsigned rm = insn & 0xfu;
        if (rn == 15u || rd == 15u || rm == 15u || rd == rn || rd == rm) {
            st = ARM_UNDEFINED;
        }
        else {
            uint32_t addr = c->r[rn];
            if (prepare_sync_address(c, addr, 4u, true, &st)) {
                /* The base ARMv6 STREX performs its write translation before
                 * consulting the local monitor. A stale/cleared monitor cannot
                 * turn an unmapped or read-only VA into a harmless status=1. */
                uint32_t ignored_pa;
                uint32_t fsr = arm_mmu_translate(c, addr, ARM_ACCESS_WRITE,
                                                 cpu_is_priv(c), &ignored_pa);
                if (fsr != 0u) {
                    note_abort(c, fsr, addr);
                } else if (c->excl_valid && c->excl_addr == addr) {
                    mem_w32(c, addr, c->r[rm]);
                    if (!c->abort_pending) c->r[rd] = 0;  /* 0 = stored */
                } else {
                    c->r[rd] = 1;                         /* 1 = failed */
                }
                c->excl_valid = false; /* monitor is consumed either way */
            }
        }
    /*
     * The rest of the ARMv6K exclusive family. These are not exotic: the real
     * kernel's 64-bit atomics go through LDREXD/STREXD (OSAddAtomic64 is the
     * first one the boot reaches), and the byte and halfword forms appear
     * throughout the prelinked kexts. Rd must be even for the doubleword
     * forms, which pair Rd with Rd+1.
     */
    } else if ((insn & 0x0ff00ff0u) == 0x01b00f90u) {     /* LDREXD Rd,Rd+1,[Rn] */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        if (rn == 15u || rd & 1u || rd == 14u) { st = ARM_UNDEFINED; }
        else {
            uint32_t addr = c->r[rn];
            if (prepare_sync_address(c, addr, 8u, false, &st)) {
                uint32_t lo = mem_r32(c, addr);
                if (!c->abort_pending) {
                    uint32_t hi = mem_r32(c, addr + 4u);
                    if (!c->abort_pending) {
                        c->r[rd] = lo; c->r[rd + 1] = hi;
                        c->excl_valid = true;
                        c->excl_addr  = addr;
                    }
                }
            }
        }
    } else if ((insn & 0x0ff00ff0u) == 0x01a00f90u) {     /* STREXD Rd,Rm,Rm+1,[Rn] */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        unsigned rm = insn & 0xfu;
        if (rn == 15u || rd == 15u || rm & 1u || rm == 14u ||
            rd == rn || rd == rm || rd == rm + 1u) {
            st = ARM_UNDEFINED;
        }
        else {
            uint32_t addr = c->r[rn];
            if (prepare_sync_address(c, addr, 8u, true, &st)) {
                if (c->excl_valid && c->excl_addr == addr) {
                    mem_w32(c, addr, c->r[rm]);
                    if (!c->abort_pending) {
                        mem_w32(c, addr + 4u, c->r[rm + 1u]);
                        if (!c->abort_pending) c->r[rd] = 0;
                    }
                } else {
                    c->r[rd] = 1;
                }
                c->excl_valid = false;
            }
        }
    } else if ((insn & 0x0ff00ff0u) == 0x01d00f90u) {     /* LDREXB Rd,[Rn] */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        if (rn == 15u || rd == 15u) { st = ARM_UNDEFINED; }
        else {
            uint32_t addr = c->r[rn];
            uint32_t v = mem_r8(c, addr);
            if (!c->abort_pending) {
                c->r[rd] = v;
                c->excl_valid = true;
                c->excl_addr  = addr;
            }
        }
    } else if ((insn & 0x0ff00ff0u) == 0x01c00f90u) {     /* STREXB Rd,Rm,[Rn] */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        unsigned rm = insn & 0xfu;
        if (rn == 15u || rd == 15u || rm == 15u || rd == rn || rd == rm) {
            st = ARM_UNDEFINED;
        }
        else {
            uint32_t addr = c->r[rn];
            if (c->excl_valid && c->excl_addr == addr) {
                mem_w8(c, addr, (uint8_t)c->r[rm]);
                if (!c->abort_pending) c->r[rd] = 0;
            } else {
                c->r[rd] = 1;
            }
            c->excl_valid = false;
        }
    } else if ((insn & 0x0ff00ff0u) == 0x01f00f90u) {     /* LDREXH Rd,[Rn] */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        if (rn == 15u || rd == 15u) { st = ARM_UNDEFINED; }
        else {
            uint32_t addr = c->r[rn];
            if (prepare_sync_address(c, addr, 2u, false, &st)) {
                uint32_t v = mem_r16(c, addr);
                if (!c->abort_pending) {
                    c->r[rd] = v;
                    c->excl_valid = true;
                    c->excl_addr  = addr;
                }
            }
        }
    } else if ((insn & 0x0ff00ff0u) == 0x01e00f90u) {     /* STREXH Rd,Rm,[Rn] */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        unsigned rm = insn & 0xfu;
        if (rn == 15u || rd == 15u || rm == 15u || rd == rn || rd == rm) {
            st = ARM_UNDEFINED;
        }
        else {
            uint32_t addr = c->r[rn];
            if (prepare_sync_address(c, addr, 2u, true, &st)) {
                if (c->excl_valid && c->excl_addr == addr) {
                    mem_w16(c, addr, (uint16_t)c->r[rm]);
                    if (!c->abort_pending) c->r[rd] = 0;
                } else {
                    c->r[rd] = 1;
                }
                c->excl_valid = false;
            }
        }
    /*
     * SWP/SWPB — the pre-ARMv6 atomic. Deprecated in favour of LDREX/STREX but
     * still present in shipping code of this vintage. On a single core an
     * uninterrupted read-then-write is a faithful implementation.
     */
    } else if ((insn & 0x0fb00ff0u) == 0x01000090u) {     /* SWP / SWPB */
        unsigned rn = (insn >> 16) & 0xfu, rd = (insn >> 12) & 0xfu;
        unsigned rm = insn & 0xfu;
        bool byte = (insn >> 22) & 1u;
        if (rn == 15u || rd == 15u || rm == 15u || rn == rm || rn == rd) {
            st = ARM_UNDEFINED;
        }
        else {
            uint32_t addr = c->r[rn];
            bool access = true;
            if (!byte && (addr & 3u) != 0u &&
                (c->cp15.sctlr & (ARM_SCTLR_U | ARM_SCTLR_A)) != 0u) {
                note_alignment_abort(c, addr, true);
                access = false;
            }
            if (access) {
                /* With U=0,A=0, mem_r32 performs the legacy aligned read plus
                 * rotate and mem_w32 performs the aligned unrotated write. */
                uint32_t old = byte ? mem_r8(c, addr) : mem_r32(c, addr);
                if (!c->abort_pending) {
                    if (byte) mem_w8(c, addr, (uint8_t)c->r[rm]);
                    else      mem_w32(c, addr, c->r[rm]);
                    if (!c->abort_pending) c->r[rd] = old;
                }
            }
        }
    } else if ((insn & 0x0e000000u) == 0x00000000u &&
               (insn & 0x00000090u) == 0x00000090u) {
        /* Remaining multiply/synchronisation extension space after the exact
         * MUL, long-multiply, exclusive, and SWP decoders above.
         * bits[27:25]==000, bit7==1, bit4==1. The bit25==0
         * requirement (mask 0x0e000000, not 0x0c000000) is essential: immediate
         * data-processing sets bit25 and its imm8 may itself have bits 7 and 4
         * set (e.g. MOV r0,#0x90), so it must NOT be trapped here. The ARMv5TE
         * DSP multiplies have bit4==0 and are decoded explicitly above. Any
         * encoding left here is unsupported/reserved, so trap rather than
         * corrupting architectural state. */
        st = ARM_UNDEFINED;
    } else if (insn == 0xe320f003u) {                 /* ARMv6K WFI hint */
        /* This hint is a NOP in User mode; only privileged code waits. */
        if (cpu_is_priv(c) && c->bus && c->bus->wait_for_interrupt)
            (void)c->bus->wait_for_interrupt(c->bus->ctx);
    } else if ((insn & 0x0f000010u) == 0x0e000010u) {     /* MCR / MRC (CP15) */
        st = exec_coprocessor(c, pc, insn);
    } else if ((insn & 0x0f000e10u) == 0x0e000a00u ||     /* VFP CDP          */
               (insn & 0x0e000e00u) == 0x0c000a00u) {     /* VFP LDC/STC/MCRR */
        /* The rest of the VFP11 encoding space. The MCR/MRC form came back
         * from exec_coprocessor above; these three groups have no CP15
         * counterpart and so reach the unit directly. */
        st = vfp_execute(c, pc, insn, &g_vfp_bus);
    } else if ((insn & 0x0f000000u) == 0x0f000000u) {     /* SWI / SVC */
        arm_svc_result_t result = privileged_svc_result(c, pc, insn);
        if (result == ARM_SVC_ERROR) return ARM_HALT;
        if (result == ARM_SVC_REDIRECTED)
            next = c->r[15];
        else if (result != ARM_SVC_HANDLED)
            take_exception(c, ARM_VEC_SWI, ARM_MODE_SVC, pc + 4, false, &next);
    } else if ((insn & 0x0c000000u) == 0x00000000u) {     /* data processing / PSR */
        st = exec_data_processing(c, pc, insn, &next);
    } else {
        st = ARM_UNDEFINED;
    }

    /* A translation fault latched during the instruction becomes a data abort.
     * LR_abt is the aborting instruction's address + 8, per the architecture. */
    if (c->abort_pending) {
        take_pending_data_abort(c, pc);
        return ARM_OK;
    }

    /* Every ARM-state encoding we declined funnels through here, wherever in
     * the decode tree it was rejected — the CDP, LDC/STC and MCRR/MRRC forms
     * of VFP all fall out of the bottom of the tree, and MCR/MRC comes back
     * from exec_coprocessor. One choke point, one rule. */
    if (st == ARM_GUEST_UNDEFINED) return take_undefined_instruction(c, pc);
    if (st == ARM_UNDEFINED) return undefined_instruction(c, pc, insn);

    if (st == ARM_OK) c->r[15] = next;
    return st;
}
