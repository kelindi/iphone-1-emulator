#ifndef PRIVATE_HANDOFF_RUNTIME_H
#define PRIVATE_HANDOFF_RUNTIME_H
#include "sdio.h"
enum { RUNTIME_CAPACITY=320, RUNTIME_GUARD=16, RUNTIME_DELAY=600 };
typedef enum { RT_EMPTY,RT_PREPARING,RT_ACTIVE,RT_INVALID } runtime_phase_t;
typedef enum { SLOT_EMPTY,SLOT_RESERVED,SLOT_COMMITTED } runtime_slot_phase_t;
typedef struct {
 uint8_t *allocation;
 size_t capacity,reserved,used;
 runtime_slot_phase_t phase;
 uint64_t token;
} runtime_slot_t;
typedef void *(*runtime_allocate_t)(void *,size_t);
typedef void (*runtime_free_t)(void *,void *);
typedef struct {
 runtime_phase_t phase;
 sdio_t *device; /* host binding only: never appended to old portable schema */
 runtime_slot_t slots[2]; /* command input and card response staging */
 runtime_allocate_t allocate;
 runtime_free_t release;
 void *allocator_ctx;
 uint64_t generation,now,deadline;
 bool have_time,publish_marker;
} runtime_t;
/* Transactional source-state admission. Requires an empty runtime, canonical
 * old phone already loaded, and exact complete supported received image. */
bool runtime_prepare(runtime_t *,sdio_t *,uint64_t,runtime_allocate_t,runtime_free_t,void *,bool);
bool runtime_image_supported(const sdio_t *);
void runtime_destroy(runtime_t *);
bool runtime_ready(const runtime_t *);
/* Actual finite storage operations. Channel0=input,1=response; physical size
 *320 only. These APIs do not implement guest CMD53 or command execution. */
bool runtime_reserve(runtime_t *,unsigned,size_t,uint64_t *);
bool runtime_commit(runtime_t *,unsigned,uint64_t,const uint8_t *,size_t);
bool runtime_read(const runtime_t *,unsigned,uint64_t,size_t,uint8_t *,size_t);
bool runtime_release(runtime_t *,unsigned,uint64_t);
bool runtime_cancel(runtime_t *,unsigned,uint64_t);
bool runtime_execute_command(runtime_t *); /* unsupported; never fakes a reply */
/* Tiny hooks in isolated controller copy; unattached devices are unchanged. */
void runtime_step(sdio_t *,uint64_t);
void runtime_unavailable(sdio_t *);
void runtime_card_reset(sdio_t *);
bool runtime_register(sdio_t *,uint32_t,uint32_t,uint32_t *);
#endif
