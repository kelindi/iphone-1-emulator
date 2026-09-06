#include "dt_patch.h"
#include <string.h>

#define DT_NAME_LEN 32u
#define DT_PROP_HDR 36u

uint32_t s5l_dt_ld32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void s5l_dt_st32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

size_t s5l_dt_header(const uint8_t *b, size_t len, size_t off,
                     uint32_t *np, uint32_t *nc) {
    if (off > len || len - off < 8u) return 0;
    *np = s5l_dt_ld32(b + off); *nc = s5l_dt_ld32(b + off + 4u);
    if (*np > 4096u || *nc > 4096u) return 0;
    return off + 8u;
}

static size_t s5l_dt_props_end(const uint8_t *b, size_t len, size_t off) {
    uint32_t np, nc;
    size_t p = s5l_dt_header(b, len, off, &np, &nc);
    (void)nc;
    if (!p) return 0;
    for (uint32_t i = 0; i < np; i++) {
        if (p > len || len - p < DT_PROP_HDR) return 0;
        uint32_t n = s5l_dt_ld32(b + p + DT_NAME_LEN) & 0x7fffffffu;
        size_t pad = ((size_t)n + 3u) & ~(size_t)3u;
        if (pad > len - p - DT_PROP_HDR) return 0;
        p += DT_PROP_HDR + pad;
    }
    return p;
}

size_t s5l_dt_subtree_end(const uint8_t *b, size_t len, size_t off,
                          unsigned depth) {
    uint32_t np, nc;
    if (depth > 32u || !s5l_dt_header(b, len, off, &np, &nc)) return 0;
    size_t p = s5l_dt_props_end(b, len, off);
    for (uint32_t i = 0; p && i < nc; i++)
        p = s5l_dt_subtree_end(b, len, p, depth + 1u);
    return p;
}

uint8_t *s5l_dt_prop(uint8_t *b, size_t len, size_t off, const char *name,
                     uint32_t *value_len) {
    uint32_t np, nc;
    size_t p = s5l_dt_header(b, len, off, &np, &nc);
    (void)nc;
    if (!p) return NULL;
    for (uint32_t i = 0; i < np; i++) {
        if (p > len || len - p < DT_PROP_HDR) return NULL;
        char nm[DT_NAME_LEN + 1u];
        memcpy(nm, b + p, DT_NAME_LEN); nm[DT_NAME_LEN] = '\0';
        uint32_t n = s5l_dt_ld32(b + p + DT_NAME_LEN) & 0x7fffffffu;
        size_t pad = ((size_t)n + 3u) & ~(size_t)3u;
        if (pad > len - p - DT_PROP_HDR) return NULL;
        if (!strcmp(nm, name)) {
            if (value_len) *value_len = n;
            return b + p + DT_PROP_HDR;
        }
        p += DT_PROP_HDR + pad;
    }
    return NULL;
}

size_t s5l_dt_path(uint8_t *b, size_t len, const char *path) {
    size_t off = 0;
    while (path && *path) {
        while (*path == '/') path++;
        if (!*path) break;
        const char *slash = strchr(path, '/');
        size_t n = slash ? (size_t)(slash - path) : strlen(path);
        uint32_t np, nc;
        if (!s5l_dt_header(b, len, off, &np, &nc)) return DT_NO_NODE;
    size_t child = s5l_dt_props_end(b, len, off), found = DT_NO_NODE;
        for (uint32_t i = 0; child && i < nc; i++) {
            uint32_t vl = 0;
            const uint8_t *nm = s5l_dt_prop(b, len, child, "name", &vl);
            if (nm && vl >= n && !memcmp(nm, path, n) &&
                (vl == n || nm[n] == '\0')) {
                found = child; break;
            }
            child = s5l_dt_subtree_end(b, len, child, 0);
        }
        if (found == DT_NO_NODE) return DT_NO_NODE;
        off = found;
        path = slash ? slash + 1 : path + n;
    }
    return off;
}

bool s5l_dt_set_u32(uint8_t *b, size_t len, const char *path,
                    const char *prop, uint32_t value) {
    size_t node = s5l_dt_path(b, len, path);
    uint32_t n = 0;
    uint8_t *p = node == DT_NO_NODE ? NULL :
                 s5l_dt_prop(b, len, node, prop, &n);
    if (!p || n != 4u) return false;
    s5l_dt_st32(p, value); return true;
}

bool s5l_dt_set_reg(uint8_t *b, size_t len, const char *path,
                    const char *prop, uint32_t base, uint32_t size) {
    size_t node = s5l_dt_path(b, len, path);
    uint32_t n = 0;
    uint8_t *p = node == DT_NO_NODE ? NULL :
                 s5l_dt_prop(b, len, node, prop, &n);
    if (!p || n != 8u) return false;
    s5l_dt_st32(p, base); s5l_dt_st32(p + 4u, size); return true;
}

bool s5l_dt_compatible_exact(uint8_t *b, size_t len, const char *path,
                              const char *expected) {
    size_t node = s5l_dt_path(b, len, path);
    uint32_t n = 0;
    const uint8_t *p = node == DT_NO_NODE ? NULL :
                       s5l_dt_prop(b, len, node, "compatible", &n);
    size_t want = expected ? strlen(expected) : 0;
    return p && n == want + 1u && p[want] == '\0' &&
           !memcmp(p, expected, want);
}

bool s5l_dt_memmap_add(uint8_t *b, size_t len, const char *key,
                       uint32_t address, uint32_t size) {
    size_t node = s5l_dt_path(b, len, "chosen/memory-map");
    uint32_t np, nc;
    size_t p = node == DT_NO_NODE ? 0 : s5l_dt_header(b, len, node, &np, &nc);
    (void)nc;
    if (!p) return false;
    size_t slot = 0;
    for (uint32_t i = 0; i < np; i++) {
        if (p > len || len - p < DT_PROP_HDR) return false;
        char nm[DT_NAME_LEN + 1u];
        memcpy(nm, b + p, DT_NAME_LEN); nm[DT_NAME_LEN] = '\0';
        uint32_t n = s5l_dt_ld32(b + p + DT_NAME_LEN) & 0x7fffffffu;
        size_t pad = ((size_t)n + 3u) & ~(size_t)3u;
        if (pad > len - p - DT_PROP_HDR) return false;
        if (!strcmp(nm, key) && n == 8u) { slot = p; break; }
        if (!slot && n == 8u && !strncmp(nm, "MemoryMapReserved-", 18u) &&
            s5l_dt_ld32(b + p + 36u) == 0u &&
            s5l_dt_ld32(b + p + 40u) == 0u) slot = p;
        p += DT_PROP_HDR + pad;
    }
    if (!slot || strlen(key) >= DT_NAME_LEN) return false;
    memset(b + slot, 0, DT_NAME_LEN);
    memcpy(b + slot, key, strlen(key));
    s5l_dt_st32(b + slot + DT_PROP_HDR, address);
    s5l_dt_st32(b + slot + DT_PROP_HDR + 4u, size);
    return true;
}
