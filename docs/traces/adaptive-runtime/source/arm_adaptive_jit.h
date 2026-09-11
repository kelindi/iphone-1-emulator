#ifndef IPHONE1_ARM_ADAPTIVE_JIT_H
#define IPHONE1_ARM_ADAPTIVE_JIT_H
#include "arm.h"
#include <stddef.h>

#define ARM_ADAPTIVE_ABI 2u
#define ARM_ADAPTIVE_MAX_DEPENDENCIES 8u

enum arm_adaptive_exit {
    ADAPT_EXIT_INVALID = 0,
    ADAPT_EXIT_BUDGET = 1,
    ADAPT_EXIT_EDGE = 2,
    ADAPT_EXIT_BEFORE = 3,
    ADAPT_EXIT_STALE = 4,
    ADAPT_EXIT_CONTEXT = 5,
    ADAPT_EXIT_BOUNDARY = 6
};
typedef struct {
    uint32_t retired, reason, status, resume_pc;
    uint32_t instruction_pc, dependency, detail, reserved;
} arm_adaptive_result_t;
typedef void (*arm_adaptive_fn)(arm_cpu_t *, uint32_t, arm_adaptive_result_t *);
_Static_assert(sizeof(arm_adaptive_result_t) == 32, "ABI2 result size");
_Static_assert(offsetof(arm_adaptive_result_t, reason) == 4, "ABI2 reason");
_Static_assert(offsetof(arm_adaptive_result_t, status) == 8, "ABI2 status");
_Static_assert(offsetof(arm_adaptive_result_t, resume_pc) == 12, "ABI2 resume");
_Static_assert(offsetof(arm_adaptive_result_t, instruction_pc) == 16, "ABI2 instruction");
_Static_assert(offsetof(arm_adaptive_result_t, dependency) == 20, "ABI2 dependency");
_Static_assert(offsetof(arm_adaptive_result_t, detail) == 24, "ABI2 detail");
_Static_assert(offsetof(arm_adaptive_result_t, reserved) == 28, "ABI2 reserved");

/* Validation is fail-closed, not rollback. A rejected module may already have
 * modified CPU/RAM; the caller MUST stop execution rather than interpret again.
 * This pure check neither credits cycles nor touches guest memory. */
static inline bool arm_adaptive_result_valid(const arm_cpu_t *cpu,
                                             const arm_adaptive_result_t *r,
                                             uint32_t budget,
                                             uint32_t dependencies) {
    if (!cpu || !r || dependencies > ARM_ADAPTIVE_MAX_DEPENDENCIES ||
        r->retired > budget || r->status != ARM_OK || r->reserved || r->detail ||
        r->resume_pc != cpu->r[15] ||
        (r->dependency != UINT32_MAX && r->dependency >= dependencies))
        return false;
    switch (r->reason) {
    case ADAPT_EXIT_BUDGET:
        return r->retired == budget;
    case ADAPT_EXIT_EDGE:
    case ADAPT_EXIT_BOUNDARY:
        return r->retired > 0;
    case ADAPT_EXIT_BEFORE:
    case ADAPT_EXIT_STALE:
    case ADAPT_EXIT_CONTEXT:
        return r->retired < budget && r->instruction_pc == r->resume_pc;
    default:
        return false;
    }
}

/* Caller must admit/pin a unit and provide a machine-derived budget first.
 * 1: validated result, prefix credited exactly once; 0: no function invoked;
 * -1: invalid execution result, fatal to the current run (never retry it).
 * WASM traps bypass this return and must be caught by the host recovery path.
 */
static inline int arm_adaptive_invoke(arm_cpu_t *cpu, arm_adaptive_fn fn,
                                      uint32_t budget, uint32_t dependencies,
                                      arm_adaptive_result_t *result) {
    if (!cpu || !result) return -1;
    *result = (arm_adaptive_result_t){0, ADAPT_EXIT_INVALID, ARM_OK,
                                     cpu->r[15], cpu->r[15], UINT32_MAX, 0, 0};
    if (!budget) {
        result->reason = ADAPT_EXIT_BUDGET;
        return 0;
    }
    if (!fn || dependencies > ARM_ADAPTIVE_MAX_DEPENDENCIES) return 0;
    uint64_t cycles = cpu->cycles;
    uint32_t initial_thumb = cpu->cpsr & ARM_CPSR_T;
    uint32_t protected_cpsr = cpu->cpsr & ~(ARM_CPSR_N | ARM_CPSR_Z |
        ARM_CPSR_C | ARM_CPSR_V | ARM_CPSR_T);
    fn(cpu, budget, result);
    if (cpu->cycles != cycles ||
        ((cpu->cpsr & ARM_CPSR_T) != initial_thumb &&
         (result->reason != ADAPT_EXIT_BOUNDARY || !result->retired)) ||
        (cpu->cpsr & ~(ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C |
                      ARM_CPSR_V | ARM_CPSR_T)) != protected_cpsr ||
        !arm_adaptive_result_valid(cpu, result, budget, dependencies))
        return -1;
    cpu->cycles += result->retired;
    return 1;
}
#endif
