#include "board.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
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
        CHECK(m.spi[0].slaves[0].transfer != NULL &&
              m.spi[0].slaves[0].ctx == &m.spi[0] &&
              m.spi[0].slaves[1].transfer == NULL && m.spi[0].cs == 0u,
              "m68 display transmitter wiring\n");
        /* Reproduce the old loader's SPI0 route and the exact first wake
         * command. It must remain pending until restore fixes the host route,
         * then complete through real FIFO data and the normal IRQ path. */
        m.spi[0].cs = 1u;
        s5l_spi_write(&m.spi[0], SPI_CNT, 2u);
        s5l_spi_write(&m.spi[0], SPI_SETUP, 0x11b8u);
        s5l_spi_write(&m.spi[0], SPI_TXDATA, 0x16u);
        s5l_spi_write(&m.spi[0], SPI_TXDATA, 0xffu);
        CHECK(m.spi[0].tx_level == 2u && m.spi[0].rx_level == 0u &&
              !s5l_spi_irq(&m.spi[0]), "legacy wake was not pending\n");
        uint8_t *saved = NULL;
        size_t saved_len = 0u;
        CHECK(snapshot_save_mem(&m, &saved, &saved_len) == SNAP_OK,
              "legacy M68 route capture failed\n");
        if (saved) {
            CHECK(snapshot_load_mem(&m, saved, saved_len) == SNAP_OK,
                  "legacy M68 route restore failed\n");
            CHECK(m.spi[0].cs == 0u && m.spi[0].tx_level == 2u,
                  "restore lost queued data or selected the wrong endpoint\n");
            s5l_spi_step(&m.spi[0]);
            CHECK(m.spi[0].tx_level == 0u && m.spi[0].rx_level == 2u &&
                  m.spi[0].words_left == 0u && s5l_spi_irq(&m.spi[0]),
                  "display transmitter write did not complete\n");
            CHECK(s5l_spi_read(&m.spi[0], SPI_RXDATA) == 0u &&
                  s5l_spi_read(&m.spi[0], SPI_RXDATA) == 0u,
                  "write-only transmitter invented register data\n");
            free(saved);
        }
        m.spi[0].cs = 2u;
        CHECK(snapshot_save_mem(&m, &saved, &saved_len) == SNAP_ERR_CORRUPT,
              "unknown M68 transmitter route was accepted\n");
        m.spi[0].cs = S5L_SPI_LCD_READ_PENDING | 1u;
        m.spi[0].words_left = 1u;
        CHECK(snapshot_save_mem(&m, &saved, &saved_len) == SNAP_ERR_CORRUPT,
              "Merlot read phase was accepted on the M68 transmitter\n");
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
