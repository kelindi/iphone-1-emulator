#include "arm_adaptive_guard.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t code[1024] = {1,0,0xa0,0xe3};
static uint8_t *host(void *ctx,uint32_t address,uint32_t length){(void)ctx;return address==0x1000&&length==1024?code:NULL;}
int main(void) {
    arm_cpu_t cpu = {0};
    arm_bus_t bus={.host_ram=host}; cpu.bus=&bus;
    cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    cpu.tlb_gen = cpu.fetch_gen = 7; cpu.fetch_priv = true;
    cpu.fetch_host = code; cpu.fetch_blk = 0x1000;
    arm_adaptive_fetch_witness_t w;
    assert(arm_adaptive_capture_fetch(&cpu, 1, &w));
    arm_cpu_t before = cpu;
    assert(arm_adaptive_guard_instruction(&cpu,&w,1,0x1000,0xe3a00001,4));
    assert(memcmp(&before,&cpu,sizeof cpu)==0);
    code[0] = 2;
    assert(!arm_adaptive_guard_instruction(&cpu,&w,1,0x1000,0xe3a00001,4));
    code[0] = 1;
    assert(!arm_adaptive_guard_instruction(&cpu,&w,2,0x1000,0xe3a00001,4));
    assert(!arm_adaptive_guard_instruction(&cpu,&w,1,0x1002,0xe3a00001,4));
    assert(!arm_adaptive_guard_instruction(&cpu,&w,1,0x1400,0xe3a00001,4));
    cpu.tlb_gen++;
    assert(!arm_adaptive_guard_instruction(&cpu,&w,1,0x1000,0xe3a00001,4));
    cpu = before; cpu.cp15.dacr++;
    assert(!arm_adaptive_guard_instruction(&cpu,&w,1,0x1000,0xe3a00001,4));
    cpu = before; cpu.abort_pending = true;
    assert(!arm_adaptive_guard_instruction(&cpu,&w,1,0x1000,0xe3a00001,4));
    cpu = before; cpu.cpsr &= ~ARM_CPSR_I; cpu.irq_line = true;
    assert(!arm_adaptive_guard_instruction(&cpu,&w,1,0x1000,0xe3a00001,4));
    cpu = before; cpu.cpsr = ARM_MODE_USR;
    assert(!arm_adaptive_capture_fetch(&cpu,1,&w));
    cpu = before; cpu.fetch_gen--;
    assert(!arm_adaptive_capture_fetch(&cpu,1,&w));
    cpu = before; cpu.fetch_host = NULL;
    assert(!arm_adaptive_capture_fetch(&cpu,1,&w));
    puts("PASS: fetch witness identity, lifetime/context, literal mutation and side-effect-free refusals");
}
