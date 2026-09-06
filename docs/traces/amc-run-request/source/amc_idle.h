#ifndef ISOLATED_AMC_IDLE_H
#define ISOLATED_AMC_IDLE_H
#include <stdbool.h>
#include <stdint.h>
/* Restricted never-started engine model. Completion is synchronous and is
 * explicitly a functional approximation, not measured silicon timing. */
typedef struct {
    bool enabled, core_reset, de_complete, upload;
    bool upload_observed, input_pending;
    unsigned post_prefix;
    uint32_t idle_config[19];
    uint32_t divider, channel_config;
    unsigned prefix;
} amc_idle_t;
typedef enum { AMC_ACCEPT, AMC_REJECT, AMC_UPLOAD_END, AMC_RUN_REQUEST } amc_result_t;
void amc_idle_init(amc_idle_t *s);
void amc_idle_dependencies(amc_idle_t *s, uint32_t clock_gate, uint32_t power_gate);
bool amc_idle_observe_upload(amc_idle_t *s);
amc_result_t amc_idle_reset(amc_idle_t *s, uint32_t command);
amc_result_t amc_idle_read(const amc_idle_t *s, uint32_t off, unsigned width, uint32_t *out);
amc_result_t amc_idle_write(amc_idle_t *s, uint32_t off, unsigned width, uint32_t value);
#endif
