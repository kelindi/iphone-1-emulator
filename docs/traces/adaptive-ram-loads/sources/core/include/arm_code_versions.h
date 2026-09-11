#ifndef IPHONE1_ARM_CODE_VERSIONS_H
#define IPHONE1_ARM_CODE_VERSIONS_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* Derived host state only. Physical granules are relative to the machine RAM
 * base, which the owner must ensure is 1KiB aligned in physical address space.
 * Version zero is never live; exhaustion invalidates the entire lifetime. */
typedef struct {
    uint8_t *ram;
    uint32_t bytes, count;
    uint32_t *versions;
    bool exhausted;
} arm_code_versions_t;

static inline void arm_code_versions_clear(arm_code_versions_t *v) {
    if (!v) return;
    free(v->versions);
    *v = (arm_code_versions_t){0};
}
static inline bool arm_code_versions_init(arm_code_versions_t *v,
                                          uint8_t *ram, uint32_t bytes) {
    if (!v || v->versions || !ram || !bytes ||
        (uintptr_t)ram > UINTPTR_MAX - (bytes - 1u)) return false;
    uint32_t count = (bytes - 1u) / 1024u + 1u;
    uint32_t *versions = malloc((size_t)count * sizeof *versions);
    if (!versions) return false;
    for (uint32_t i = 0; i < count; i++) versions[i] = 1u;
    *v = (arm_code_versions_t){ram, bytes, count, versions, false};
    return true;
}
static inline bool arm_code_versions_span(const arm_code_versions_t *v,
        const uint8_t *host, uint32_t len, uint32_t *first, uint32_t *last) {
    if (!v || !v->versions || v->exhausted || !len || !host ||
        (uintptr_t)host < (uintptr_t)v->ram) return false;
    uintptr_t off = (uintptr_t)host - (uintptr_t)v->ram;
    if (off >= v->bytes || len > v->bytes - off) return false;
    *first = (uint32_t)off / 1024u;
    *last = ((uint32_t)off + len - 1u) / 1024u;
    return true;
}
static inline uint32_t arm_code_version(const arm_code_versions_t *v,
                                        const uint8_t *host) {
    uint32_t first, last;
    return arm_code_versions_span(v, host, 1, &first, &last)
        ? v->versions[first] : 0u;
}
/* Call after committed writes, even if the bytes happen to be unchanged.
 * A bad notification fails closed; a zero-length write is a harmless no-op.
 * All touched granules advance once per notification, including both ends. */
static inline bool arm_code_versions_written(arm_code_versions_t *v,
                                             const uint8_t *host, uint32_t len) {
    if (!v || !v->versions || v->exhausted) return false;
    if (!len) return true;
    uint32_t first, last;
    if (!arm_code_versions_span(v, host, len, &first, &last)) {
        v->exhausted = true;
        return false;
    }
    for (uint32_t i = first; i <= last; i++) {
        if (v->versions[i] == UINT32_MAX) {
            v->exhausted = true;
            return false;
        }
    }
    for (uint32_t i = first; i <= last; i++) v->versions[i]++;
    return true;
}
#endif
