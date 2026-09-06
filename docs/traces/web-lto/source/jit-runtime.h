#ifndef IPHONE1_WEB_JIT_RUNTIME_H
#define IPHONE1_WEB_JIT_RUNTIME_H
#include "soc.h"
/* One machine per browser worker. Passing NULL releases the runtime binding. */
void web_jit_bind(s5l8900_t *machine);
#endif
