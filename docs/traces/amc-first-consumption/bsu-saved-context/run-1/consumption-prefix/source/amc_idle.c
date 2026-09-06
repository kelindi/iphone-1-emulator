#include "amc_idle.h"
#include <string.h>
#ifndef AMC_ARMED_PREFIX
#define AMC_ARMED_PREFIX 4
#endif
#ifndef AMC_INPUT_PREFIX
#define AMC_INPUT_PREFIX 0
#endif
void amc_idle_init(amc_idle_t *s) { memset(s, 0, sizeof *s); }
static void invalidate(amc_idle_t *s) {
    s->de_complete = s->upload = false;
    s->upload_observed = s->input_pending = s->armed = false;
    s->post_prefix = s->armed_prefix = s->input_prefix = 0u;
    s->input_observed = false;
    s->input_address = s->input_words = s->input_bytes = 0u;
    memset(s->input_config, 0, sizeof s->input_config);
    memset(s->armed_config, 0, sizeof s->armed_config);
    memset(s->idle_config, 0, sizeof s->idle_config);
    s->prefix = s->channel_config = 0u;
}
void amc_idle_dependencies(amc_idle_t *s, uint32_t clock, uint32_t power) {
    s->enabled = !(clock & 0x18u) && !(power & 0x10u);
    if (!s->enabled) { invalidate(s); s->core_reset = false; }
}
amc_result_t amc_idle_reset(amc_idle_t *s, uint32_t command) {
    if (!s->enabled || (command != 0xa000u && command != 0xa00u) ||
        (command == 0xa00u && !s->core_reset)) return AMC_REJECT;
    invalidate(s);
    if (command == 0xa000u) s->core_reset = true;
    else s->de_complete = true;
    return AMC_ACCEPT;
}
bool amc_idle_observe_upload(amc_idle_t *s) {
    /* Host calls after in-process guest-descriptor byte/coverage validation.
     * A separate original-Mach-O oracle independently checks capture artifacts
     * afterward; it does not run inside this callback. This records observation,
     * not a hardware completion or readback latch. */
    if (!s->enabled || !s->de_complete || !s->upload || s->prefix != 6u || s->upload_observed) return false;
    s->upload_observed = true;
    return true;
}
/* Observation-only expansion: no executed instructions, guest status, SRAM,
 * current address or interrupt changes. Host validates original image first. */
bool amc_idle_observe_command(amc_idle_t *s) {
    if (!s->enabled || !s->de_complete || !s->upload_observed ||
        s->post_prefix != 19u || s->input_pending || s->armed) return false;
    s->armed = true;
    return true;
}
bool amc_idle_observe_input(amc_idle_t *s, uint32_t address, uint32_t words,
                            uint32_t bytes, uint32_t ram_base, uint32_t ram_size) {
    if (!s->enabled || !s->de_complete || !s->armed || s->armed_prefix != 4u ||
        s->input_pending || s->input_observed || !bytes || bytes > 0x10000u ||
        words != (bytes+3u)/4u || (address&3u) || address < ram_base ||
        (uint64_t)address+4ull*words > (uint64_t)ram_base+ram_size ||
        (uint64_t)address+4ull*words > UINT32_MAX) return false;
    s->input_observed = true; s->input_address=address;
    s->input_words=words; s->input_bytes=bytes;
    return true;
}
amc_result_t amc_idle_read(const amc_idle_t *s, uint32_t off,
                           unsigned width, uint32_t *out) {
    if (width != 4u || !out) return AMC_REJECT;
    uint32_t v;
    switch (off) {
    case 0x101cu: v = s->input_pending ? 1u : 0u; break;
    case 0x10u: v = 0u; break; /* No engine start is accepted by this model. */
    case 0xb18u: v = s->enabled && s->de_complete ? 0x100u : 0u; break;
    case 0x184u: if (s->armed) return AMC_REJECT; v = s->channel_config; break; /* New active read semantics are unproven. */
    case 0x2000u: v = s->divider; break;
    default: return AMC_REJECT;
    }
    *out = v;
    return AMC_ACCEPT;
}
amc_result_t amc_idle_write(amc_idle_t *s, uint32_t off,
                            unsigned width, uint32_t value) {
    if (width != 4u) return AMC_REJECT;
    if (off == 0x2000u) { if (s->armed) return AMC_REJECT; s->divider = value; return AMC_ACCEPT; }
    if (!s->enabled || !s->de_complete) return AMC_REJECT;
    if (s->armed && s->input_observed) {
        /* Staged input requests only; no transfer starts in this model. */
        static const uint32_t offsets[] = {0x101cu,0x100cu,0x1000u,0x1004u,0x1008u,0xa00u};
        const uint32_t values[] = {1u,0u,1u,s->input_address,s->input_words,1u};
        if (s->input_prefix < AMC_INPUT_PREFIX && s->input_prefix < 6u &&
            off == offsets[s->input_prefix] && value == values[s->input_prefix]) {
            s->input_config[s->input_prefix++] = value;
            return AMC_ACCEPT;
        }
        if (s->input_prefix == 6u && off == 0x100cu && value == 1u) return AMC_INPUT_REQUEST;
        return AMC_REJECT;
    }
    if (s->armed) {
        /* Each admitted element must have its own retained real MMIO witness.
         * These are request records only, not status/IRQ/transfer effects. */
        static const uint32_t offsets[] = {0x204u,0x184u,0xb0cu,0x1018u};
        static const uint32_t values[] = {1u,7u,0x2000u,1u};
        if (s->armed_prefix >= AMC_ARMED_PREFIX || s->armed_prefix >= 4u ||
            off != offsets[s->armed_prefix] || value != values[s->armed_prefix]) return AMC_REJECT;
        s->armed_config[s->armed_prefix++] = value;
        return AMC_ACCEPT;
    }
    if (s->upload_observed) {
        /* Exact witnessed/predicted never-started AAC idle prefix. No event
         * producer, progress counter, IRQ, execution or sample side effect. */
        static const uint32_t off_after[] = {
            0x101cu,0x100cu,0x1000u,0x1004u,0x1008u,0xa00u,
            0x300u,0x310u,0x304u,0x314u,0x308u,0x318u,0x30cu,0x31cu,
            0x320u,0x324u,0x300u,0x304u,0xd0cu
        };
        static const uint32_t val_after[] = {
            1u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,
            0x07000001u,0x3f081603u,0x40u
        };
        if (s->post_prefix < 19u && off == off_after[s->post_prefix] && value == val_after[s->post_prefix]) {
            if (off == 0x101cu) s->input_pending = false; /* Original IRQ acknowledgement. */
            s->idle_config[s->post_prefix++] = value;
            return AMC_ACCEPT;
        }
        if (!s->armed && s->post_prefix == 19u && off == 0u && value == 1u) return AMC_RUN_REQUEST;
        return AMC_REJECT;
    }
    /* Exact original configure prefix only. These commands are recorded as
     * idle setup, with no inferred interrupt, FIFO, execution or event effect. */
    static const uint32_t offsets[] = {0xa00u, 0x184u, 0x180u, 0xb10u, 0xb04u, 0x340u};
    static const uint32_t values[] = {0u, 0u, 1u, UINT32_MAX, UINT32_MAX, 0u};
    if (s->prefix < 6u && off == offsets[s->prefix] && value == values[s->prefix]) {
        if (off == 0x184u) s->channel_config = value;
        if (off == 0x340u) s->upload = true;
        s->prefix++;
        return AMC_ACCEPT;
    }
    /* Host must independently verify all original descriptor bytes before
     * accepting this positive sentinel; no device state changes here. */
    if (s->upload && off == 0x340u && value == 1u) return AMC_UPLOAD_END;
    return AMC_REJECT;
}
