#ifndef S5LBOX_DT_PATCH_H
#define S5LBOX_DT_PATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DT_NO_NODE ((size_t)-1)

uint32_t s5l_dt_ld32(const uint8_t *p);
void s5l_dt_st32(uint8_t *p, uint32_t v);
size_t s5l_dt_header(const uint8_t *b, size_t len, size_t off,
                     uint32_t *n_props, uint32_t *n_children);
size_t s5l_dt_subtree_end(const uint8_t *b, size_t len, size_t off,
                          unsigned depth);
size_t s5l_dt_path(uint8_t *b, size_t len, const char *path);
uint8_t *s5l_dt_prop(uint8_t *b, size_t len, size_t off, const char *name,
                     uint32_t *value_len);
bool s5l_dt_set_u32(uint8_t *b, size_t len, const char *path,
                    const char *prop, uint32_t value);
bool s5l_dt_set_reg(uint8_t *b, size_t len, const char *path,
                    const char *prop, uint32_t base, uint32_t size);
bool s5l_dt_compatible_exact(uint8_t *b, size_t len, const char *path,
                              const char *expected);
bool s5l_dt_memmap_add(uint8_t *b, size_t len, const char *key,
                       uint32_t address, uint32_t size);

#endif
