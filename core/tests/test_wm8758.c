#include "board.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned pass, fail;
#define CHECK(x, msg)                                                                              \
    do {                                                                                           \
        if (x)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL line%d: %s\n", __LINE__, msg);                                            \
        }                                                                                          \
    } while (0)
static void event(s5l_i2c_t *b, unsigned e) {
    CHECK((s5l_i2c_read(b, I2C_INT) & e) != 0, "I2C completion event");
    s5l_i2c_write(b, I2C_INT, e);
}
static void begin(s5l_i2c_t *b) {
    s5l_i2c_write(b, I2C_ENABLE, 1);
    s5l_i2c_write(b, I2C_STAT, I2C_STAT_MODE_MTX | I2C_STAT_ENABLE);
    s5l_i2c_write(b, I2C_DS, WM8758_I2C_ADDR << 1);
    s5l_i2c_write(b, I2C_STAT, I2C_STAT_MODE_MTX | I2C_STAT_ENABLE | I2C_STAT_START);
    event(b, I2C_INT_BYTE);
    CHECK(!b->nak, "codec acknowledges address");
}
static void byte(s5l_i2c_t *b, unsigned v) {
    s5l_i2c_write(b, I2C_DS, v);
    s5l_i2c_write(b, I2C_CON, I2C_CON_ACKEN | I2C_CON_RESUME);
    event(b, I2C_INT_BYTE);
}
static void stop(s5l_i2c_t *b) {
    s5l_i2c_write(b, I2C_STAT, I2C_STAT_MODE_MRX | I2C_STAT_ENABLE);
    event(b, I2C_INT_STOP);
}
static void pair(s5l_i2c_t *b, unsigned a, unsigned z) {
    begin(b);
    byte(b, a);
    byte(b, z);
    CHECK(!b->nak, "codec acknowledges packed payload");
    stop(b);
}
/* Complete observed command prefix from genuine 1A543a cold boot.
 * Columns are wire byte0, byte1, then independently hooked driver reg/value. */
static const unsigned golden[][4] = {
    {0x00, 0x00, 0x00, 0x000}, {0x7b, 0x01, 0x3d, 0x101}, {0x63, 0x86, 0x31, 0x186},
    {0x69, 0xf9, 0x34, 0x1f9}, {0x6b, 0xf9, 0x35, 0x1f9}, {0x6d, 0xf9, 0x36, 0x1f9},
    {0x6f, 0xf9, 0x37, 0x1f9}, {0x70, 0x40, 0x38, 0x040}, {0x72, 0x40, 0x39, 0x040},
    {0x54, 0x06, 0x2a, 0x006}, {0x56, 0x04, 0x2b, 0x004}, {0x05, 0x80, 0x02, 0x180},
    {0x06, 0x60, 0x03, 0x060}, {0x54, 0x0f, 0x2a, 0x00f}, {0x54, 0x0d, 0x2a, 0x00d},
    {0x56, 0x00, 0x2b, 0x000}, {0x54, 0x04, 0x2a, 0x004}, {0x06, 0x6c, 0x03, 0x06c},
    {0x02, 0x0d, 0x01, 0x00d}, {0x08, 0x10, 0x04, 0x010}, {0x0c, 0x0d, 0x06, 0x00d},
    {0x0e, 0x00, 0x07, 0x000}, {0x1c, 0x00, 0x0e, 0x000}, {0x58, 0x00, 0x2c, 0x000},
    {0x5a, 0x40, 0x2d, 0x040}, {0x5d, 0x40, 0x2e, 0x140}, {0x64, 0x14, 0x32, 0x014},
    {0x66, 0x14, 0x33, 0x014}, {0x14, 0x03, 0x0a, 0x003}, {0x5e, 0x00, 0x2f, 0x000},
    {0x60, 0x00, 0x30, 0x000}, {0x64, 0x14, 0x32, 0x014}, {0x66, 0x14, 0x33, 0x014},
    {0x02, 0x0d, 0x01, 0x00d}, {0x06, 0x6c, 0x03, 0x06c}, {0x69, 0xf9, 0x34, 0x1f9},
    {0x6b, 0xf9, 0x35, 0x1f9}, {0x6d, 0xf9, 0x36, 0x1f9}, {0x6f, 0xf9, 0x37, 0x1f9},
    {0x48, 0x0a, 0x24, 0x00a}, {0x4f, 0xe8, 0x27, 0x1e8}, {0x4d, 0xfd, 0x26, 0x1fd},
    {0x4a, 0x01, 0x25, 0x001}, {0x0d, 0x4d, 0x06, 0x14d}, {0x0e, 0x00, 0x07, 0x000},
    {0x69, 0xf9, 0x34, 0x1f9}, {0x6b, 0xf9, 0x35, 0x1f9}, {0x6d, 0xf9, 0x36, 0x1f9},
    {0x6f, 0xf9, 0x37, 0x1f9}, {0x7b, 0x00, 0x3d, 0x100}, {0x54, 0x00, 0x2a, 0x000},
    {0x6c, 0xb9, 0x36, 0x0b9}, {0x6f, 0xb9, 0x37, 0x1b9}, {0x68, 0xa7, 0x34, 0x0a7},
    {0x6b, 0xa7, 0x35, 0x1a7}, {0x02, 0x1d, 0x01, 0x01d},
};
int main(void) {
    s5l8900_t m = {0};
    CHECK(s5l_board_init(s5l_board_find("m68"), &m, 16u << 20), "initialize M68");
    s5l_i2c_t *b = &m.i2c[0];
    CHECK(b->slave_count == 2 && b->slaves[0].addr == PCF50635_I2C_ADDR &&
              b->slaves[1].addr == WM8758_I2C_ADDR,
          "M68 appends correct codec after PMU");
    CHECK(s5l_wm8758_peek(&m.codec, 4) == 0x50 && s5l_wm8758_peek(&m.codec, 6) == 0x140,
          "unwritten registers use actual OS1 reset shadow");
    for (unsigned i = 0; i < sizeof golden / sizeof golden[0]; i++) {
        pair(b, golden[i][0], golden[i][1]);
        CHECK(s5l_wm8758_peek(&m.codec, golden[i][2]) == golden[i][3],
              "real boot command reaches correct nine-bit register");
    }
    CHECK(m.codec.packed_writes == sizeof golden / sizeof golden[0] && !m.codec.refused_writes,
          "whole observed boot sequence accepted");
    pair(b, 2, 0x3f);
    CHECK(s5l_wm8758_peek(&m.codec, 1) == 0x3f,
          "WM8758 power bits are not masked as WM8991 identity");
    begin(b);
    byte(b, 0x63);
    byte(b, 0x20);
    byte(b, 0x55);
    CHECK(b->nak, "third payload byte refused");
    stop(b);
    CHECK(s5l_wm8758_peek(&m.codec, 0x31) == 0x186, "overlong write never partially commits");
    begin(b);
    byte(b, 0x63);
    stop(b);
    CHECK(s5l_wm8758_peek(&m.codec, 0x31) == 0x186, "truncated write never commits");
    pair(b, 0xfe, 0xff);
    CHECK(!m.codec.written[127] && !m.codec.regs[127],
          "unobserved register rejected without corrupting storage");
    const s5l_i2c_slave_t *s = &b->slaves[1];
    CHECK(!s->start(s->ctx, true), "unimplemented codec read is NAK, not fabricated ID");
    s->stop(s->ctx);
    pair(b, 0, 0);
    CHECK(s5l_wm8758_peek(&m.codec, 1) == 0 && s5l_wm8758_peek(&m.codec, 0x31) == 2 &&
              m.codec.regs[0] == 0 && !m.codec.written[0],
          "software reset restores shadow defaults without storing command");
    uint8_t *saved = NULL;
    size_t size = 0;
    CHECK(snapshot_save_mem(&m, &saved, &size) == SNAP_OK, "save codec reset state");
    if (saved) {
        pair(b, 2, 0x3f);
        CHECK(snapshot_load_mem(&m, saved, size) == SNAP_OK && s5l_wm8758_peek(&m.codec, 1) == 0 &&
                  b->slaves[1].ctx == &m.codec,
              "restore reset register state and preserve host wiring");
        free(saved);
        saved = NULL;
    }
    begin(b);
    byte(b, 0x63);
    CHECK(snapshot_save_mem(&m, &saved, &size) == SNAP_OK, "snapshot halfway through packed write");
    if (saved) {
        byte(b, 0x11);
        stop(b);
        CHECK(snapshot_load_mem(&m, saved, size) == SNAP_OK, "restore pending packed write");
        byte(b, 0x86);
        stop(b);
        CHECK(s5l_wm8758_peek(&m.codec, 0x31) == 0x186,
              "restored second byte commits exactly once");
        free(saved);
        saved = NULL;
    }
    pair(b, 2, 0x3f);
    CHECK(snapshot_save_mem(&m, &saved, &size) == SNAP_OK,
          "M68 snapshot preserves all nine-bit power bits");
    free(saved);
    saved = NULL;
    m.codec.regs[1] = 0x200;
    CHECK(snapshot_save_mem(&m, &saved, &size) == SNAP_ERR_CORRUPT,
          "reject wider-than-nine-bit M68 state");
    m.codec.regs[1] = 0x3f;
    s5l8900_free(&m);
    CHECK(s5l_board_init(s5l_board_find("n82"), &m, 16u << 20), "initialize N82");
    CHECK(m.i2c[0].slaves[1].addr == WM8991_I2C_ADDR &&
              s5l_wm8991_peek(&m.codec, 0) == WM8991_ID_VALUE,
          "N82 retains WM8991 identity and route");
    m.codec.regs[1] = WM8991_PWR1_PROBE;
    m.codec.written[1] = 1;
    CHECK(snapshot_save_mem(&m, &saved, &size) == SNAP_ERR_CORRUPT,
          "N82 snapshot still rejects discriminator bit");
    s5l8900_free(&m);
    printf("WM8758 %u passed, %u failed\n", pass, fail);
    return fail != 0;
}
