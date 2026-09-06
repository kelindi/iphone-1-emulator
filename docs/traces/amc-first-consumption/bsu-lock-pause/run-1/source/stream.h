#ifndef AMC_STREAM_OBSERVER_H
#define AMC_STREAM_OBSERVER_H
#include "consume.h"
#include "retirement.h"
#include "amc_engine.h"
#define STREAM_CAP 64
#define STREAM_READ_CAP 65536
/* Generation0 is encoded as ordinal1; slot_latest=0 means never published. */
typedef struct {uint64_t cycles;uint32_t generation,slot,address,width,value,pc,cpsr,owner;} stream_read_record;
_Static_assert(sizeof(stream_read_record)==40,"stream read ledger ABI");
typedef struct {
 uint32_t ordinal,slot;uint64_t published,decoded,copied,samples;
 uint32_t events,mask;consume_observer reads;
 bool ipc,ack,released,pending;uint64_t ipc_cycles,ack_cycles;
 guest_step release_step;uint64_t release_callback,release_end;uint32_t release_pc,release_cpsr,release_opcode,after_pc,after_cpsr,release_address,driver,index;
} stream_generation;
typedef struct {unsigned count,slot_latest[2],read_count,release_count,pending_generation,ipc_pending;stream_generation gen[STREAM_CAP];stream_read_record ledger[STREAM_READ_CAP];} stream_observer;
bool stream_prepare(const stream_observer*,const amc_engine*);
bool stream_publish(stream_observer*,const amc_engine*,unsigned,uint64_t);
bool stream_read(stream_observer*,const amc_engine*,uint32_t,unsigned,uint32_t,uint32_t,uint32_t,uint64_t);
bool stream_release_begin(stream_observer*,const amc_engine*,uint32_t,unsigned,uint32_t,const guest_step*,uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,bool);
bool stream_release_finish(stream_observer*,const amc_engine*,const guest_step*,uint64_t,uint32_t,uint32_t,bool);
bool stream_admit(const amc_engine*,const amc_physical_ram*,unsigned,uint32_t,uint32_t,uint32_t);
void stream_ack(stream_observer*,uint64_t);
bool stream_ipc(stream_observer*,unsigned,uint64_t,bool);
#endif
