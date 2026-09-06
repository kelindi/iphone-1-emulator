#ifndef PRIVATE_SDIO_CHECKPOINT_H
#define PRIVATE_SDIO_CHECKPOINT_H
#include "sdio.h"
#include "snapshot.h"
typedef struct { uint64_t ticks,cycles; uint32_t pc,cpu_hz,ram_base,ram_size; } checkpoint_instant_t;
typedef struct checkpoint_phone checkpoint_phone_t;
struct checkpoint_phone { s5l8900_t *machine; sdio_t device; arm_bus_t underlying;
 void (*observe)(checkpoint_phone_t *,bool,uint32_t,uint32_t,unsigned,bool);
};
bool checkpoint_bundle_save(const sdio_t *,const s5l_mtz1_t *,const checkpoint_instant_t *,uint8_t **,size_t *);
bool checkpoint_bundle_load(sdio_t *,s5l_mtz1_t *,checkpoint_instant_t *,const uint8_t *,size_t);
bool checkpoint_side_save(const sdio_t *,const checkpoint_instant_t *,uint8_t **,size_t *);
/* Transactional decode; output callback pointers are preserved, never decoded. */
bool checkpoint_side_load(sdio_t *,checkpoint_instant_t *,const uint8_t *,size_t);
/* Requires a zero-initialized phone; refuses to replace a live one. M68 only. */
bool checkpoint_phone_init(checkpoint_phone_t *,uint32_t,uint32_t,const sdio_config_t *);
void checkpoint_phone_free(checkpoint_phone_t *);
void checkpoint_sync(checkpoint_phone_t *);
bool checkpoint_pair_save(const checkpoint_phone_t *,uint8_t **,size_t *,uint8_t **,size_t *);
/* Creates a new independent heap-owned phone. Never overwrites a caller's live phone.
 * Caller validates manifest/hashes before entry. Geometry comes from the checked
 * sidecar, bounded here to private experiment RAM <=256MiB. */
checkpoint_phone_t *checkpoint_pair_load(const uint8_t *,size_t,const uint8_t *,size_t);
bool checkpoint_arch_equal(const uint8_t *,size_t,const arm_cpu_t *,const uint8_t *,size_t,const arm_cpu_t *);
void checkpoint_original_event(bool,uint32_t,uint32_t,unsigned,bool);
bool checkpoint_continue_compare(s5l8900_t *,sdio_t *,void (*)(void),const uint8_t *,size_t,const uint8_t *,size_t,uint32_t,uint64_t,const char *);
#endif
