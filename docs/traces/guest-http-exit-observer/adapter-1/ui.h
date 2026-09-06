#ifndef EXIT_UI_H
#define EXIT_UI_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {unsigned x,y,w,h;const char*name;} reference;
typedef struct {uint16_t x,y;} tap;
typedef struct {const char*name;unsigned before,after,count;tap taps[15];} stage;
typedef struct {unsigned stage,phase,tap,stable;uint64_t stage_insn,stage_ticks,phase_ticks,last_scan;bool done,failed,down;const char*reason;} ui_state;
/* Actual native scanout bytes, no replacement/normalization of guest output. */
unsigned ui_error(const uint8_t*,const uint8_t*,const reference*);
/* -1=lift, 1=touch, 0=none. Caller must apply through original device API. */
int ui_step(ui_state*,uint64_t,uint64_t,bool,tap*);
void ui_observe(ui_state*,bool,uint64_t);
#endif
