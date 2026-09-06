#ifndef AMC_POST_OBSERVATION_H
#define AMC_POST_OBSERVATION_H
#include "peek.h"
#define POST_CAP 128
/* Plain scalar receipt; the hook is staged before executing the instruction. */
typedef struct {
 uint32_t pc,cpsr,regs[16],after_pc,after_cpsr,after_lr,retired,expected_pc,expected_mode,available;
 uint64_t cycles,after_cycles;
 uint8_t code[32],stack[64],object[64],output[16];
} post_record;
_Static_assert(sizeof(post_record)==296,"bounded post record ABI");
typedef struct {unsigned count;bool pending;post_record staged,records[POST_CAP];} post_observer;
bool post_target(uint32_t);
bool post_begin(post_observer*,const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t);
bool post_finish(post_observer*,const arm_cpu_t*);
bool post_fifo(uint32_t,unsigned);
#endif
