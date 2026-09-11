#ifndef IPHONE1_ARM_ADAPTIVE_DISPATCH_H
#define IPHONE1_ARM_ADAPTIVE_DISPATCH_H

#include "arm_adaptive_jit.h"

/* Optional execution policy; the legacy WASM seed remains independently usable.
 * The caller supplies a machine-derived budget. A successful call validates
 * its result and credits only CPU cycles, never machine retirement/device time.
 * 0: not admitted, no instruction effects; 1: validated result; -1: fatal result.
 * Fatal execution maps to ARM_HALT in the scheduler: stop for host diagnosis,
 * without injecting a guest Undefined exception or replaying guest effects.
 * The runtime/host must retain the specific fatal diagnostic for recovery.
 */
/* Called BEFORE restore/boot/free or other unobserved RAM lifetime changes.
 * Unrelated CPUs succeed unchanged. Failure forbids the requested mutation;
 * active execution or a full retirement queue must never free live code/RAM. */
int arm_adaptive_forget(arm_cpu_t *cpu);
extern unsigned arm_adaptive_enabled;
int arm_adaptive_try(arm_cpu_t *cpu, uint32_t budget,
                     arm_adaptive_result_t *result);

#endif
