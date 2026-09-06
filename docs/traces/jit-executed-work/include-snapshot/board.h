#ifndef S5LBOX_BOARD_H
#define S5LBOX_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "soc.h"

typedef struct s5l_board {
    const char *name;
    const char *model;
    const char *dt_compatible;
    uint32_t ram_base;
    uint32_t ram_size;
    unsigned lcd_width, lcd_height;
    bool (*attach)(s5l8900_t *m);
} s5l_board_t;

extern const s5l_board_t s5l_board_m68;
extern const s5l_board_t s5l_board_n82;

const s5l_board_t *s5l_board_find(const char *name);
bool s5l_board_init(const s5l_board_t *b, s5l8900_t *m,
                    uint32_t ram_size_override);
bool s5l_board_check_devicetree(const s5l_board_t *b, const uint8_t *dt,
                                size_t len, char *why, size_t cap);

#endif
