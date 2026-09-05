#include "board.h"
#include <stdio.h>
#include <string.h>

static uint32_t ld32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool root_compatible(const uint8_t *dt, size_t len,
                            const char *want) {
    if (!dt || len < 8u || !want) return false;
    uint32_t np = ld32(dt), nc = ld32(dt + 4);
    if (np > 4096u || nc > 4096u) return false;
    size_t p = 8u;
    for (uint32_t i = 0; i < np; i++) {
        if (p > len || len - p < 36u) return false;
        char name[33];
        memcpy(name, dt + p, 32u);
        name[32] = '\0';
        uint32_t n = ld32(dt + p + 32u) & 0x7fffffffu;
        size_t padded = ((size_t)n + 3u) & ~(size_t)3u;
        if (padded > len - p - 36u) return false;
        if (!strcmp(name, "compatible")) {
            const uint8_t *v = dt + p + 36u;
            size_t off = 0;
            while (off < n) {
                size_t left = n - off;
                size_t sl = strnlen((const char *)v + off, left);
                if (sl == left) return false;
                if (!strcmp((const char *)v + off, want)) return true;
                off += sl + 1u;
            }
            return false;
        }
        p += 36u + padded;
    }
    return false;
}

static void why_set(char *why, size_t cap, const char *text) {
    if (!why || !cap) return;
    snprintf(why, cap, "%s", text);
}

bool s5l_board_check_devicetree(const s5l_board_t *b, const uint8_t *dt,
                                size_t len, char *why, size_t cap) {
    if (!b || !dt || len < 8u) {
        why_set(why, cap, "device tree is too small");
        return false;
    }
    if (!root_compatible(dt, len, b->dt_compatible)) {
        why_set(why, cap, "root compatible does not contain board model");
        return false;
    }
    if (why && cap) why[0] = '\0';
    return true;
}

const s5l_board_t *s5l_board_find(const char *name) {
    if (!name) return NULL;
    if (!strcmp(name, "m68")) return &s5l_board_m68;
    if (!strcmp(name, "n82")) return &s5l_board_n82;
    return NULL;
}

bool s5l_board_init(const s5l_board_t *b, s5l8900_t *m,
                    uint32_t ram_size_override) {
    if (!b || !m || !s5l8900_init_soc(m, b->ram_base,
                                       ram_size_override ? ram_size_override :
                                       b->ram_size))
        return false;
    if (!b->attach(m)) {
        s5l8900_free(m);
        return false;
    }
    return true;
}
