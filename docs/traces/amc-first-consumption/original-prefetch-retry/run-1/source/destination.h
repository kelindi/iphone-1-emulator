#ifndef AMC_COPY_DESTINATION_H
#define AMC_COPY_DESTINATION_H
#include "peek.h"
typedef struct {
 arm_cpu_t *context;
 bool captured,finished,caller_available;
 uint64_t cycles,finished_cycles;
 uint32_t regs[16],cpsr,sctlr,ttbr0,ttbr1,ttbcr,dacr,source_physical;
 uint32_t maps[5],lengths[5],map_count,caller_address;
 uint8_t code[32],prologue[32],frame[20],caller[32],bytes[4096];
} destination_observer;
bool destination_begin(destination_observer*,const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t);
bool destination_finish(destination_observer*,const uint8_t*,uint32_t,uint32_t,const uint8_t*,uint64_t);
void destination_destroy(destination_observer*);
#endif
