#include "board.h"

static bool n82_attach(s5l8900_t *m) {
    if (m->i2c[0].slaves[0].start &&
        m->spi[0].slaves[S5L_SPI0_LCD_CS].transfer &&
        m->spi[1].slaves[0].transfer)
        return true;
    s5l_i2c_slave_t pmu, codec;
    s5l_pcf50635_bind(&m->pmu, &pmu);
    if (!s5l_i2c_attach(&m->i2c[0], &pmu)) return false;
    s5l_wm8991_bind(&m->codec, &codec);
    if (!s5l_i2c_attach(&m->i2c[0], &codec)) return false;

    s5l_spi_slave_t lcd;
    s5l_spi_lcd_bind(&lcd, &m->spi[0]);
    if (!s5l_spi_attach(&m->spi[0], S5L_SPI0_LCD_CS, &lcd)) return false;
    m->spi[0].cs = S5L_SPI0_LCD_CS;

    s5l_mtz2_reset(&m->mtz2);
    s5l_spi_slave_t touch;
    s5l_mtz2_bind(&m->mtz2, &touch);
    if (!s5l_spi_attach(&m->spi[1], 0u, &touch)) return false;
    if (!s5l_gpio_watch(&m->gpio, MTZ2_PIN_RESET, &m->mtz2,
                        s5l_mtz2_reset_pin)) m->stub_declare_failures++;
    if (!s5l_gpio_watch(&m->gpio, MTZ2_PIN_SELECT, &m->mtz2,
                        s5l_mtz2_select_pin)) m->stub_declare_failures++;
    if (!s5l_gpio_watch(&m->gpio, MTZ2_PIN_POWER, &m->mtz2,
                        s5l_mtz2_power_pin)) m->stub_declare_failures++;
    return true;
}

const s5l_board_t s5l_board_n82 = {
    "n82", "iPhone1,2", "N82AP", 0x08000000u, 128u << 20, 320u, 480u,
    n82_attach
};
