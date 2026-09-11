#include "arm_adaptive_jit.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned calls;
static void valid_prefix(arm_cpu_t *cpu, uint32_t budget, arm_adaptive_result_t *r) {
    ++calls; cpu->r[0] += budget; cpu->r[15] += 4 * budget;
    *r = (arm_adaptive_result_t){budget, ADAPT_EXIT_BUDGET, ARM_OK,
                               cpu->r[15], cpu->r[15], 0, 0, 0};
}
static void double_credit(arm_cpu_t *cpu, uint32_t budget, arm_adaptive_result_t *r) {
    valid_prefix(cpu, budget, r); cpu->cycles += budget;
}
static void corrupt_mode(arm_cpu_t *cpu, uint32_t budget, arm_adaptive_result_t *r) {
    valid_prefix(cpu, budget, r); cpu->cpsr ^= 1;
}
static void wrong_interwork(arm_cpu_t *cpu, uint32_t budget, arm_adaptive_result_t *r) {
    valid_prefix(cpu, budget, r); cpu->cpsr ^= ARM_CPSR_T;
}
static void valid_interwork(arm_cpu_t *cpu, uint32_t budget, arm_adaptive_result_t *r) {
    wrong_interwork(cpu, budget, r); r->reason = ADAPT_EXIT_BOUNDARY;
}
int main(void) {
    arm_cpu_t cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu.r[15] = 0x1000; cpu.cpsr = ARM_MODE_SYS;
    arm_adaptive_result_t r = {0, ADAPT_EXIT_BUDGET, ARM_OK, 0x1000,
                               0x1000, UINT32_MAX, 0, 0};
    assert(arm_adaptive_result_valid(&cpu, &r, 0, 0));
    r.reason = ADAPT_EXIT_INVALID;
    assert(!arm_adaptive_result_valid(&cpu, &r, 0, 0));
    for (unsigned reason = 1; reason <= 6; ++reason) {
        r.reason = reason;
        r.retired = 2;
        assert(arm_adaptive_result_valid(&cpu, &r, reason == 1 ? 2 : 3, 1));
        r.retired = 4;
        assert(!arm_adaptive_result_valid(&cpu, &r, 3, 1));
    }
    r.reason = ADAPT_EXIT_BEFORE; r.retired = 0;
    assert(arm_adaptive_result_valid(&cpu, &r, 1, 1));
    r.instruction_pc += 4;
    assert(!arm_adaptive_result_valid(&cpu, &r, 1, 1));
    r.instruction_pc -= 4;
    r.dependency = 1;
    assert(!arm_adaptive_result_valid(&cpu, &r, 1, 1));
    r.dependency = 0;
    assert(arm_adaptive_result_valid(&cpu, &r, 1, 1));
    r.reason = ADAPT_EXIT_EDGE;
    assert(!arm_adaptive_result_valid(&cpu, &r, 1, 1));
    r.retired = 1;
    assert(arm_adaptive_result_valid(&cpu, &r, 2, 1));
    r.resume_pc += 4;
    assert(!arm_adaptive_result_valid(&cpu, &r, 2, 1));
    r.resume_pc -= 4;
    r.status = ARM_UNDEFINED;
    assert(!arm_adaptive_result_valid(&cpu, &r, 2, 1));
    r.status = ARM_OK;
    r.reserved = 1;
    assert(!arm_adaptive_result_valid(&cpu, &r, 2, 1));
    r.reserved = 0; r.detail = 1;
    assert(!arm_adaptive_result_valid(&cpu, &r, 2, 1));
    assert(cpu.cycles == 0);
    assert(arm_adaptive_invoke(&cpu, valid_prefix, 0, 1, &r) == 0);
    assert(calls == 0 && cpu.cycles == 0);
    assert(arm_adaptive_invoke(&cpu, valid_prefix, 3, 1, &r) == 1);
    assert(calls == 1 && cpu.cycles == 3 && cpu.r[0] == 3);
    assert(arm_adaptive_invoke(&cpu, double_credit, 2, 1, &r) == -1);
    assert(cpu.cycles == 5); /* invalid callee's effects cannot be rolled back */
    assert(arm_adaptive_invoke(&cpu, corrupt_mode, 1, 1, &r) == -1);
    assert(cpu.cycles == 5); /* rejected result receives no additional credit */
    assert(arm_adaptive_invoke(&cpu, wrong_interwork, 1, 1, &r) == -1);
    assert(cpu.cycles == 5);
    assert(arm_adaptive_invoke(&cpu, valid_interwork, 1, 1, &r) == 1);
    assert(cpu.cycles == 6);
    puts("ABI2 result contract: valid exits and malformed results checked");
}
