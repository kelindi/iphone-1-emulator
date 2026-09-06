#ifndef OWN_EXIT_OBSERVER_H
#define OWN_EXIT_OBSERVER_H
#include "arm.h"
#include <stddef.h>
typedef struct {uint32_t pc,mode;uint8_t code[32];} qualified_site;
typedef struct {uint32_t proc,pid,task;char name[16];} qualified_process;
typedef struct {uint32_t sctlr,ttbr0,ttbr1,ttbcr,dacr,context_id;} qualified_space;
typedef struct {uint32_t pc,cpsr,lr,args[4],proc,pid,task;uint64_t cycles;} entry_observation;
/* These are read-only ENTRY observations, not instruction-retirement or
 * completed-syscall claims. Actual continuation wiring remains unimplemented. */
bool observe_kernel_target(const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t,const qualified_site*,const qualified_process*,entry_observation*);
bool observe_own_site(const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t,const qualified_site*,const qualified_space*,entry_observation*);
#endif
