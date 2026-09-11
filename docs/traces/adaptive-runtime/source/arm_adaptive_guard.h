#ifndef IPHONE1_ARM_ADAPTIVE_GUARD_H
#define IPHONE1_ARM_ADAPTIVE_GUARD_H
#include "arm.h"

/* The runtime owns epoch and must change it before reset/restore/RAM replacement
 * or generation wrap. Witnesses retain host pointers only inside that lifetime.
 * Capture is permitted only from the interpreter's already validated fetch
 * cache. Neither capture nor guard walks tables, performs MMIO, or changes CPU.
 */
typedef struct {
    const uint8_t *host;
    uint64_t epoch;
    uint32_t block, generation, mode, thumb;
    uint32_t sctlr, ttbr0, ttbr1, ttbcr, dacr, fcse_pid, context_id;
} arm_adaptive_fetch_witness_t;

static inline bool arm_adaptive_cpu_eligible(const arm_cpu_t *cpu) {
    if (!cpu || cpu->arch != ARM_ARCH_V6_ARM1176 || cpu->abort_pending ||
        (cpu->fiq_line && !(cpu->cpsr & ARM_CPSR_F)) ||
        (cpu->irq_line && !(cpu->cpsr & ARM_CPSR_I))) return false;
    switch (cpu->cpsr & ARM_CPSR_MODE_MASK) {
    case ARM_MODE_USR: case ARM_MODE_FIQ: case ARM_MODE_IRQ:
    case ARM_MODE_SVC: case ARM_MODE_ABT: case ARM_MODE_UND:
    case ARM_MODE_SYS: return true;
    default: return false;
    }
}
static inline bool arm_adaptive_capture_fetch(const arm_cpu_t *cpu,
                                               uint64_t epoch,
                                               arm_adaptive_fetch_witness_t *out) {
    if (!out || !epoch || !arm_adaptive_cpu_eligible(cpu) || !cpu->fetch_host ||
        cpu->fetch_gen != cpu->tlb_gen || (cpu->fetch_blk & 1023u) ||
        cpu->fetch_priv != ((cpu->cpsr & ARM_CPSR_MODE_MASK) != ARM_MODE_USR))
        return false;
    *out = (arm_adaptive_fetch_witness_t){cpu->fetch_host, epoch,
        cpu->fetch_blk, cpu->tlb_gen, cpu->cpsr & ARM_CPSR_MODE_MASK,
        cpu->cpsr & ARM_CPSR_T, cpu->cp15.sctlr, cpu->cp15.ttbr0,
        cpu->cp15.ttbr1, cpu->cp15.ttbcr, cpu->cp15.dacr,
        cpu->cp15.fcse_pid, cpu->cp15.context_id};
    return true;
}
static inline bool arm_adaptive_guard_instruction(const arm_cpu_t *cpu,
        const arm_adaptive_fetch_witness_t *w, uint64_t epoch,
        uint32_t pc, uint32_t raw, unsigned width) {
    if (!w || !w->host || !epoch || w->epoch != epoch ||
        !arm_adaptive_cpu_eligible(cpu) || w->generation != cpu->tlb_gen ||
        w->mode != (cpu->cpsr & ARM_CPSR_MODE_MASK) ||
        w->thumb != (cpu->cpsr & ARM_CPSR_T) ||
        w->sctlr != cpu->cp15.sctlr || w->ttbr0 != cpu->cp15.ttbr0 ||
        w->ttbr1 != cpu->cp15.ttbr1 || w->ttbcr != cpu->cp15.ttbcr ||
        w->dacr != cpu->cp15.dacr || w->fcse_pid != cpu->cp15.fcse_pid ||
        w->context_id != cpu->cp15.context_id ||
        width != (w->thumb ? 2u : 4u) || (pc & (width - 1u)) ||
        (pc & ~1023u) != w->block || (width == 2u && raw > 0xffffu))
        return false;
    if (arm_fetch_cache_peek(cpu, pc, w->mode != ARM_MODE_USR) != w->host)
        return false;
    const uint8_t *p = w->host + (pc & 1023u);
    uint32_t actual = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    if (width == 4u) actual |= ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return actual == raw;
}
#endif
