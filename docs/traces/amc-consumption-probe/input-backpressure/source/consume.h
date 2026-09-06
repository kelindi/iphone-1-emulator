#ifndef AMC_CONSUME_OBSERVER_H
#define AMC_CONSUME_OBSERVER_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
enum { CONSUME_BASE=0x2200f220u, CONSUME_BYTES=4096, CONSUME_RECORD_CAP=65536 };
typedef struct {
 bool active;
 uint8_t expected[CONSUME_BYTES],seen[CONSUME_BYTES],coverage[CONSUME_BYTES];
 unsigned covered,reads;
} consume_observer;
typedef enum { CONSUME_IGNORE,CONSUME_ACCEPT,CONSUME_REJECT } consume_result;
void consume_begin(consume_observer*,const uint8_t*);
consume_result consume_read(consume_observer*,uint32_t,unsigned,uint32_t,bool,unsigned);
bool consume_complete(const consume_observer*);
bool consume_released(uint32_t,unsigned,uint32_t,unsigned,unsigned);
#endif
