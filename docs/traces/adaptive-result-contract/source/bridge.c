#include "arm_adaptive_jit.h"
#include <emscripten.h>
#include <string.h>
static arm_cpu_t cpu;
static arm_adaptive_result_t result;
EM_JS(void, expose_memory, (), {Module.jitMemory = wasmMemory;});
EMSCRIPTEN_KEEPALIVE void bridge_reset(void) {
    memset(&cpu, 0, sizeof cpu); memset(&result, 0, sizeof result);
    cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    expose_memory();
}
EMSCRIPTEN_KEEPALIVE uintptr_t bridge_cpu(void) {return (uintptr_t)&cpu;}
EMSCRIPTEN_KEEPALIVE uintptr_t bridge_result(void) {return (uintptr_t)&result;}
EMSCRIPTEN_KEEPALIVE unsigned bridge_offset(unsigned n) {
    return n == 0 ? offsetof(arm_cpu_t,r) : n == 1 ? offsetof(arm_cpu_t,cpsr) : offsetof(arm_cpu_t,cycles);
}
EMSCRIPTEN_KEEPALIVE int bridge_invoke(uintptr_t fn, unsigned budget, unsigned dependencies) {
    return arm_adaptive_invoke(&cpu,(arm_adaptive_fn)fn,budget,dependencies,&result);
}
