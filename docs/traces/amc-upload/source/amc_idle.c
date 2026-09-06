#include "amc_idle.h"
#include <string.h>
void amc_idle_init(amc_idle_t *s) { memset(s, 0, sizeof *s); }
static void invalidate(amc_idle_t *s) {
    s->de_complete = s->upload = false;
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
amc_result_t amc_idle_read(const amc_idle_t *s, uint32_t off,
                           unsigned width, uint32_t *out) {
    if (width != 4u || !out) return AMC_REJECT;
    uint32_t v;
    switch (off) {
    case 0x10u: v = 0u; break; /* No engine start is accepted by this model. */
    case 0xb18u: v = s->enabled && s->de_complete ? 0x100u : 0u; break;
    case 0x184u: v = s->channel_config; break;
    case 0x2000u: v = s->divider; break;
    default: return AMC_REJECT;
    }
    *out = v;
    return AMC_ACCEPT;
}
amc_result_t amc_idle_write(amc_idle_t *s, uint32_t off,
                            unsigned width, uint32_t value) {
    if (width != 4u) return AMC_REJECT;
    if (off == 0x2000u) { s->divider = value; return AMC_ACCEPT; }
    if (!s->enabled || !s->de_complete) return AMC_REJECT;
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
