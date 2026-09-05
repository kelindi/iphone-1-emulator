#include "board.h"
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(x, ...) do { if (!(x)) { printf("FAIL: " __VA_ARGS__); fail++; } } while (0)

int main(void) {
    const s5l_board_t *m68 = s5l_board_find("m68");
    const s5l_board_t *n82 = s5l_board_find("n82");
    CHECK(m68 && n82, "board lookup\n");
    CHECK(!s5l_board_find("unknown"), "unknown board accepted\n");
    s5l8900_t m;
    memset(&m, 0, sizeof m);
    CHECK(s5l_board_init(m68, &m, 256u << 20), "m68 init failed\n");
    if (m.ram) {
        CHECK(m.spi[1].slaves[0].transfer != NULL, "m68 LCD absent\n");
        CHECK(m.spi[0].slaves[0].transfer == NULL &&
              m.spi[0].slaves[1].transfer == NULL, "m68 spi0 populated\n");
        s5l8900_free(&m);
    }
    {
        uint8_t tiny[8] = {0};
        char why[64];
        CHECK(!s5l_board_check_devicetree(m68, tiny, sizeof tiny, why,
                                          sizeof why), "tiny DT accepted\n");
        CHECK(why[0] != '\0', "tiny DT had no reason\n");
    }
    printf("board: %s\n", fail ? "failed" : "passed");
    return fail != 0;
}
