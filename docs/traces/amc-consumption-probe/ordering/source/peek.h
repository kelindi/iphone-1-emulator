#ifndef AMC_PEEK_H
#define AMC_PEEK_H
#include "arm.h"
#include <stddef.h>
bool amc_peek_translate(const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t,uint32_t,uint32_t*);
bool amc_peek_ram(const arm_cpu_t*,const uint8_t*,uint32_t,uint32_t,uint32_t,void*,size_t);
#endif
