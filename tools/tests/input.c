/* Exercise the actual native dispatch against the real controller models.
 * Firmware-ready fixtures isolate host command delivery from firmware boot. */
#define main iphone1_main
#include "../iphone1.c"
#undef main

static unsigned checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void consume_z1(s5l8900_t *m) {
    s5l_spi_slave_t *slave = &m->spi[2].slaves[0];
    unsigned length = m->mtz1.pending_len + 3u;
    CHECK(m->mtz1.pending_len != 0u);
    s5l_mtz1_select_pin(slave->ctx, false);
    for (unsigned i = 0; i < length; i++)
        slave->transfer(slave->ctx, i ? 0u : 0x47u);
    s5l_mtz1_select_pin(slave->ctx, true);
    CHECK(m->mtz1.pending_len == 0u);
}

static void test_m68_release(bool move_first) {
    s5l8900_t m;
    CHECK(s5l_board_init(s5l_board_find("m68"), &m, 16u << 20));
    m.mtz1.firmware = true;
    m.mtz1.powered = true;
    input_event_t event = {.kind=INPUT_TOUCH, .phase=MTZ2_PHASE_MAKE_TOUCH,
                           .x=100u, .y=200u};
    CHECK(input_touch(&m, &event));
    CHECK(m.mtz1.held_n == 1u && m.mtz1.pending[17] == MTZ2_PHASE_MAKE_TOUCH);
    if (move_first) {
        consume_z1(&m);
        event.phase = MTZ2_PHASE_TOUCHING;
        event.x = 150u;
        CHECK(input_touch(&m, &event));
        CHECK(m.mtz1.pending[17] == MTZ2_PHASE_TOUCHING);
    }
    uint8_t pending_phase = m.mtz1.pending[17];
    event.phase = MTZ2_PHASE_BREAK_TOUCH;
    CHECK(input_touch(&m, &event));
    CHECK(m.mtz1.lift_pending && m.mtz1.pending[17] == pending_phase);
    consume_z1(&m); // The release cannot overwrite an unread MAKE or MOVE.
    s5l_mtz1_tick(&m.mtz1, MTZ1_FRAME_TICKS);
    CHECK(m.mtz1.held_n == 0u && !m.mtz1.lift_pending);
    CHECK(m.mtz1.pending[17] == MTZ2_PHASE_BREAK_TOUCH);
    consume_z1(&m);
    s5l_mtz1_tick(&m.mtz1, MTZ1_FRAME_TICKS * 3u);
    CHECK(m.mtz1.pending_len == 0u && m.mtz1.held_n == 0u);
    CHECK(input_touch(&m, &event)); // A repeated lift must remain idle.
    CHECK(m.mtz1.pending_len == 0u && !m.mtz1.lift_pending);
    s5l8900_free(&m);
}

static void read_z2(s5l_spi_slave_t *slave, uint8_t *rx,
                    unsigned length, uint8_t phase) {
    uint8_t tx[MTZ2_PAYLOAD_LIMIT + 32] = {MTZ2_OP_FRAME_Z2, 1u, phase};
    unsigned sum = MTZ2_OP_FRAME_Z2 + 1u + phase;
    tx[length - 2u] = (uint8_t)sum;
    tx[length - 1u] = (uint8_t)(sum >> 8);
    s5l_mtz2_select_pin(slave->ctx, false);
    for (unsigned i = 0; i < length; i++) rx[i] = slave->transfer(slave->ctx, tx[i]);
    s5l_mtz2_select_pin(slave->ctx, true);
}

static void test_n82_explicit_break(void) {
    s5l8900_t m;
    CHECK(s5l_board_init(s5l_board_find("n82"), &m, 16u << 20));
    m.mtz2.in_reset = false;
    m.mtz2.hbpp_mode = false;
    input_event_t event = {.kind=INPUT_TOUCH, .phase=MTZ2_PHASE_MAKE_TOUCH,
                           .x=100u, .y=200u};
    s5l_spi_slave_t *slave = &m.spi[1].slaves[0];
    uint8_t rx[MTZ2_PAYLOAD_LIMIT + 32];
    const unsigned phases[] = {MTZ2_PHASE_MAKE_TOUCH, MTZ2_PHASE_TOUCHING,
                               MTZ2_PHASE_BREAK_TOUCH};
    for (unsigned i = 0; i < 3u; i++) {
        event.phase = phases[i];
        CHECK(input_touch(&m, &event));
        read_z2(slave, rx, MTZ2_FRAME_LEN, 0u);
        unsigned length = rx[1] | (unsigned)rx[2] << 8;
        CHECK(length > 2u && length + 5u <= sizeof rx);
        read_z2(slave, rx, length + 5u, 1u);
        CHECK(rx[0] == MTZ2_OP_FRAME_Z2);
        CHECK(rx[5] == 0xccu && rx[5 + 3] == 1u);
        CHECK(rx[5 + 10 + 1] == phases[i]);
        CHECK(m.mtz2.frame_len == 0u && !m.mtz2.atn);
    }
    s5l8900_free(&m);
}

int main(void) {
    test_m68_release(false);
    test_m68_release(true);
    test_n82_explicit_break();
    printf("native input: %u checks passed\n", checks);
    return EXIT_SUCCESS;
}
