#include "board.h"
#include <string.h>

/*
 * Apple CHRP NVRAM partition header (IODTNVRAM::initNVRAMImage):
 *   +0  signature      +1  checksum
 *   +2  length, little-endian halfword, in 16-byte units, header included
 *   +4  name, 12 bytes
 * The image walk advances by `length`, so a blank (all-zero or all-0xff
 * erased) copy never terminates: xnu-933 has no `length < 16` guard.  Real
 * units ship with a "common" partition spanning each 8 KiB copy.
 */
static uint8_t chrp_checksum(const uint8_t *hdr) {
    uint8_t csum = 0;
    for (unsigned i = 0; i < 16u; i++) {
        uint8_t isum = (uint8_t)(csum + hdr[i]);
        if (isum < csum) isum++;
        csum = isum;
    }
    return csum;
}

static void m68_program_nvram(s5l8900_t *m, uint32_t off) {
    uint8_t img[S5L8900_M68_NVRAM_LEN];
    memset(img, 0, sizeof img);
    img[0] = 0x70; /* kIODTNVRAMOFPartitionName signature */
    img[2] = (uint8_t)(sizeof img / 16u);
    img[3] = (uint8_t)((sizeof img / 16u) >> 8);
    memcpy(img + 4, "common", 6);
    img[1] = chrp_checksum(img);
    s5l_nor_program(&m->nor, off, img, sizeof img);
}

static bool m68_attach(s5l8900_t *m) {
    if (s5l8900_overlaps(m->ram_base, m->ram_size,
                         S5L8900_M68_NOR_BASE, S5L8900_NOR_SIZE))
        return false;
    m->nor_base = S5L8900_M68_NOR_BASE;
    m68_program_nvram(m, S5L8900_M68_NVRAM_OFF);
    m68_program_nvram(m, S5L8900_M68_NVRAM_OFF + S5L8900_M68_NVRAM_LEN);

    s5l_i2c_slave_t pmu;
    s5l_pcf50635_bind(&m->pmu, &pmu);
    if (!s5l_i2c_attach(&m->i2c[0], &pmu)) return false;
    s5l_i2c_slave_t codec;
    s5l_wm8758_reset(&m->codec);
    s5l_wm8758_bind(&m->codec, &codec);
    if (!s5l_i2c_attach(&m->i2c[0], &codec)) return false;

    s5l_spi_slave_t mpl;
    s5l_spi_mpl_bind(&mpl, &m->spi[0]);
    if (!s5l_spi_attach(&m->spi[0], 0u, &mpl)) return false;
    m->spi[0].cs = 0u;

    s5l_spi_slave_t lcd;
    s5l_spi_lcd_bind(&lcd, &m->spi[1]);
    if (!s5l_spi_attach(&m->spi[1], 0u, &lcd)) return false;
    m->spi[1].cs = 0u;

    s5l_spi_slave_t touch;
    s5l_mtz1_reset(&m->mtz1);
    m->mtz1.trace = m->trace_devices;
    s5l_mtz1_bind(&m->mtz1, &touch);
    if (!s5l_spi_attach(&m->spi[2], 0u, &touch)) return false;
    if (!s5l_gpio_watch(&m->gpio, MTZ1_PIN_SELECT, &m->mtz1,
                        s5l_mtz1_select_pin)) m->stub_declare_failures++;
    if (!s5l_gpio_watch(&m->gpio, MTZ1_PIN_POWER, &m->mtz1,
                        s5l_mtz1_power_pin)) m->stub_declare_failures++;
    return true;
}

const s5l_board_t s5l_board_m68 = {
    "m68", "iPhone1,1", "M68AP", 0x08000000u, 128u << 20, 320u, 480u,
    m68_attach
};
