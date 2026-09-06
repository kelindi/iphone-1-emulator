#ifndef IPHONE1_ARM_WASM_JIT_H
#define IPHONE1_ARM_WASM_JIT_H
#include "arm.h"
/* Optional browser-only executor. Native builds do not reference these symbols. */
extern unsigned arm_wasm_jit_enabled;
unsigned arm_wasm_jit_try(arm_cpu_t *cpu, unsigned budget);
#endif
