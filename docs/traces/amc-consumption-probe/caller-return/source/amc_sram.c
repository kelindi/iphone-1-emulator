#include "amc_sram.h"
#include <string.h>
static bool valid(uint32_t offset, unsigned width) {
    return (width == 1u || width == 2u || width == 4u) &&
           offset <= AMC_SRAM_SIZE - width;
}
bool amc_sram_ram_conflict(uint32_t base, uint32_t size) {
    return size != 0u && (uint64_t)base < (uint64_t)AMC_SRAM_BASE + AMC_SRAM_SIZE &&
           (uint64_t)base + size > AMC_SRAM_BASE;
}
void amc_sram_init(amc_sram_t *s) {
    if (s) memset(s->bytes, 0, sizeof s->bytes);
}
bool amc_sram_read(const amc_sram_t *s, uint32_t offset,
                   unsigned width, uint32_t *value) {
    if (!s || !value || !valid(offset, width)) return false;
    uint32_t result = 0u;
    for (unsigned i = 0u; i < width; i++)
        result |= (uint32_t)s->bytes[offset + i] << (8u * i);
    *value = result;
    return true;
}
bool amc_sram_write(amc_sram_t *s, uint32_t offset,
                    unsigned width, uint32_t value) {
    if (!s || !valid(offset, width)) return false;
    for (unsigned i = 0u; i < width; i++)
        s->bytes[offset + i] = (uint8_t)(value >> (8u * i));
    return true;
}
