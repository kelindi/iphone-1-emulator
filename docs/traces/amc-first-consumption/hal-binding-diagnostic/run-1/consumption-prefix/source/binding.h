#ifndef AMC_BINDING_DIAGNOSTIC_H
#define AMC_BINDING_DIAGNOSTIC_H
#include "peek.h"
enum { BIND_OK=0,BIND_ARGUMENT=1,BIND_ALLOC=2,BIND_TABLE=3,BIND_FAULT=4,BIND_PHYSICAL=5,BIND_CODE=6,BIND_PENDING=7,BIND_CAP=8,BIND_MODE=9,BIND_TARGET=10 };
typedef struct {uint32_t address,value,available;} binding_word;
typedef struct {uint32_t va,bytes,pa,fault,available;} binding_span;
typedef struct {uint32_t access,fresh,reason,available,spans,words;binding_span span[2];binding_word table[8];uint8_t code[32];} binding_walk;
typedef struct {uint32_t attempted,reason,pc,cpsr,regs[16],count,pending,target,expected_available;uint32_t sctlr,ttbr0,ttbr1,ttbcr,dacr,context_id,dfsr,ifsr,dfar,ifar;uint64_t cycles;uint8_t expected[32];binding_walk walk[4];} binding_diagnostic;
bool binding_read(binding_walk*,const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t,uint32_t,arm_access_t,bool);
/* Injectable allocator only for focused fault/immutability contracts. */
bool binding_read_with_allocator(binding_walk*,const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t,uint32_t,arm_access_t,bool,void*(*)(size_t),void(*)(void*));
#endif
