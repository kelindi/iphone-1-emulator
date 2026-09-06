#ifndef AMC_ORDERING_H
#define AMC_ORDERING_H
#include "peek.h"
enum {ORDER_CAP=128};
typedef struct {uint32_t pc,cpsr,regs[16],driver,state,output[2],flags,retired;uint64_t cycles;uint32_t after_pc,after_cpsr;uint64_t after_cycles;} order_record;
_Static_assert(sizeof(order_record)==120,"fixed observation record ABI");
typedef struct {unsigned count;bool pending;order_record staged;order_record records[ORDER_CAP];} order_log;
typedef enum {ORDER_OK,ORDER_REJECT} order_result;
bool order_target(uint32_t);
order_result order_begin(order_log*,const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t,uint32_t);
order_result order_finish(order_log*,const arm_cpu_t*);
#endif
