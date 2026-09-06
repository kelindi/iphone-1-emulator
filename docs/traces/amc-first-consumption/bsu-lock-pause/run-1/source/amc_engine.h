#ifndef ISOLATED_AMC_ENGINE_H
#define ISOLATED_AMC_ENGINE_H
#include "amc_idle.h"
#include "amc_sram.h"
#include <stddef.h>

enum { AMC_QUEUE_BYTES=65536, AMC_OUTPUT_BYTES=8192, AMC_HEADER=0xf200,
       AMC_OUTPUT0=0xf220, AMC_OUTPUT1=0x11220, AMC_SESSION=0x2a000 };
typedef enum { AMC_ENGINE_OK, AMC_ENGINE_REJECT, AMC_ENGINE_WAIT,
               AMC_ENGINE_PUBLISHED, AMC_ENGINE_FATAL } amc_engine_result;
typedef struct {
    size_t consumed, samples;
    const int16_t *pcm;
} amc_decoded;
typedef struct {
    void *(*open)(void);
    void (*close)(void *);
    bool (*configure)(void *, unsigned rate, unsigned channels);
    bool (*probe)(const uint8_t *, size_t, unsigned rate, amc_decoded *);
    bool (*decode)(void *, const uint8_t *, size_t, amc_decoded *);
} amc_codec_ops;
extern const amc_codec_ops amc_faad_ops;
typedef struct {
    /* Trusted immutable policy data, validated by the separate original-file
     * oracle. Never generated from current guest descriptors or private RAM. */
    const uint8_t *original_sram;
    const uint8_t *original_write_mask;
    size_t bytes;
} amc_program_policy;
typedef struct {
    const uint8_t *bytes;
    uint32_t base, size;
} amc_physical_ram;
typedef struct {
    amc_idle_t *idle;
    amc_sram_t *sram;
    const amc_codec_ops *codec;
    void *decoder;
    uint8_t *queue;
    void (*release)(void *);
    bool initialized, configured, fault, service, dma_active, input_pending;
    unsigned setup_phase, input_phase, session_phase, next_output;
    uint32_t events, mask, input_mask, staged_address, staged_words;
    uint32_t dma_start, dma_words, copied_words;
    bool transport_enabled;
    /* Isolated functional BSU/DMA exclusion; unfinished input ownership stays active. */
    bool bsu_locked, dma_paused;
    size_t queue_used;
    uint64_t queue_epoch, last_probe_epoch, decoded_bytes, copied_bytes;
    uint64_t decoded_frames, published_frames, sample_units, generation;
    unsigned probe_attempts;
    uint8_t init_writes[AMC_SRAM_SIZE];
    uint8_t pcm_writes[AMC_SRAM_SIZE];
    uint8_t host_writes[AMC_SRAM_SIZE];
} amc_engine;

void amc_engine_init(amc_engine *);
void amc_engine_destroy(amc_engine *);
amc_engine_result amc_engine_dependencies(amc_engine *,uint32_t clock_gate,uint32_t power_gate);
bool amc_engine_layout(uint32_t header, uint32_t first, uint32_t second, uint32_t capacity);
amc_engine_result amc_engine_start(amc_engine *,amc_idle_t *,amc_sram_t *,
    const uint8_t *actual_writes,const amc_program_policy *,const amc_codec_ops *,
    void *(*allocate)(size_t),void (*release)(void *));
amc_engine_result amc_engine_read(const amc_engine *,uint32_t,unsigned,uint32_t *);
amc_engine_result amc_engine_write(amc_engine *,const amc_physical_ram *,uint32_t,unsigned,uint32_t);
amc_engine_result amc_engine_sram_write(amc_engine *,uint32_t,unsigned,uint32_t);
amc_engine_result amc_engine_step(amc_engine *,const amc_physical_ram *);
bool amc_engine_irq(const amc_engine *);
#endif
