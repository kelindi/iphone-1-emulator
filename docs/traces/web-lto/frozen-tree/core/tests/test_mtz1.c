#include "soc.h"
#include <stdio.h>
#include <string.h>

static int pass, fail;
#define CHECK(x, ...) do { if (x) pass++; else { fail++; \
    printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void txn(s5l_spi_slave_t *s, const uint8_t *tx, uint8_t *rx,
                unsigned n) {
    s5l_mtz1_select_pin(s->ctx, false);
    for (unsigned i = 0; i < n; i++) rx[i] = s->transfer(s->ctx, tx[i]);
    s5l_mtz1_select_pin(s->ctx, true);
}

static uint16_t sum(const uint8_t *p, unsigned n) {
    uint16_t v = 0;
    while (n--) v = (uint16_t)(v + *p++);
    return v;
}

static uint32_t frame_ms(const uint8_t *rx) {
    return rx[7] | ((uint32_t)rx[8] << 8) |
           ((uint32_t)rx[9] << 16) | ((uint32_t)rx[10] << 24);
}

static unsigned read_frame(s5l_spi_slave_t *slave, uint8_t *rx) {
    const uint8_t length[8] = {0x46u};
    uint8_t request[MTZ2_PAYLOAD_LIMIT + 3u] = {0x47u};
    txn(slave, length, rx, sizeof length);
    unsigned n = ((unsigned)rx[1] << 8) | rx[2];
    CHECK(n <= MTZ2_PAYLOAD_LIMIT + 2u, "result length fits transfer");
    if (n > MTZ2_PAYLOAD_LIMIT + 2u) return 0u;
    txn(slave, request, rx, n + 1u);
    return n;
}

static void test_contact_lifecycle(void) {
    s5l_mtz1_t d = {0};
    s5l_spi_slave_t slave;
    uint8_t rx[MTZ2_PAYLOAD_LIMIT + 3u];
    s5l_mtz1_reset(&d);
    s5l_mtz1_bind(&d, &slave);
    d.firmware = true;
    s5l_mtz1_tick(&d, 6000000u);
    s5l_mt_contact_t contacts[MTZ2_CONTACT_MAX] = {
        {1u, MTZ2_PHASE_MAKE_TOUCH, 40u, 80u, 255u, 8u, 8u},
        {2u, MTZ2_PHASE_MAKE_TOUCH, 200u, 300u, 230u, 7u, 7u}
    };
    CHECK(s5l_mtz1_set_contacts(&d, contacts, 1u), "start lifecycle contact");
    s5l_mtz1_t before = d;
    contacts[0].x = 50u;
    CHECK(!s5l_mtz1_set_contacts(&d, contacts, 1u) &&
          !memcmp(&before, &d, sizeof d), "busy move cannot replace make report");
    uint8_t report[4] = {0xd0u};
    txn(&slave, report, rx, sizeof report);
    CHECK(d.pending_len == before.pending_len && d.atn &&
          d.frames_read == before.frames_read,
          "interface read preserves queued contact");
    uint8_t short_data[4] = {0x47u};
    txn(&slave, short_data, rx, sizeof short_data);
    CHECK(d.pending_len == before.pending_len && d.atn &&
          d.frames_read == before.frames_read,
          "short data read preserves unread make and checksum");
    CHECK(s5l_mtz1_set_contacts(&d, NULL, 0u), "release accepted behind pending make");
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    CHECK(d.lift_pending && d.pending[17] == MTZ2_PHASE_MAKE_TOUCH,
          "release waits for make consumption");
    read_frame(&slave, rx);
    CHECK(rx[18] == MTZ2_PHASE_MAKE_TOUCH, "first report remains make");
    CHECK(!s5l_mtz1_set_contacts(&d, contacts, 1u), "new gesture waits for old break");
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    read_frame(&slave, rx);
    CHECK(rx[18] == MTZ2_PHASE_BREAK_TOUCH && rx[33] == 0u && !d.held_n,
          "backpressured release eventually emits break");
    CHECK(s5l_mtz1_set_contacts(&d, NULL, 0u), "idle release is idempotent");

    CHECK(s5l_mtz1_set_contacts(&d, contacts, 1u), "new gesture after break");
    read_frame(&slave, rx);
    CHECK(rx[18] == MTZ2_PHASE_MAKE_TOUCH, "reused ID makes a new contact");
    contacts[0].x = 60u;
    CHECK(s5l_mtz1_set_contacts(&d, contacts, 2u), "second finger joins gesture");
    read_frame(&slave, rx);
    CHECK(rx[4] == 2u && rx[17] == 1u && rx[18] == MTZ2_PHASE_TOUCHING &&
          rx[49] == 2u && rx[50] == MTZ2_PHASE_MAKE_TOUCH,
          "old finger moves while new finger makes");
    uint32_t stamp = frame_ms(rx);
    contacts[1].x = 210u;
    CHECK(s5l_mtz1_set_contacts(&d, contacts + 1u, 1u), "remove only first finger");
    read_frame(&slave, rx);
    CHECK(rx[4] == 2u && rx[17] == 1u && rx[18] == MTZ2_PHASE_BREAK_TOUCH &&
          rx[33] == 0u && rx[37] == 0u &&
          rx[49] == 2u && rx[50] == MTZ2_PHASE_TOUCHING && rx[65] == 230u,
          "removed ID breaks with zero density; surviving ID keeps touching");
    CHECK(frame_ms(rx) > stamp, "successive host events have monotonic timestamps");
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    read_frame(&slave, rx);
    CHECK(rx[4] == 1u && rx[17] == 2u && rx[18] == MTZ2_PHASE_TOUCHING,
          "periodic report excludes lifted finger");

    before = d;
    s5l_mt_contact_t bad[2] = {contacts[1], contacts[1]};
    CHECK(!s5l_mtz1_set_contacts(&d, bad, 2u) && !memcmp(&before, &d, sizeof d),
          "duplicate IDs rejected without mutating state");
    bad[0].x = S5L_MT_PANEL_W;
    CHECK(!s5l_mtz1_set_contacts(&d, bad, 1u) && !memcmp(&before, &d, sizeof d),
          "off-panel contact rejected without mutating state");
    CHECK(!s5l_mtz1_set_contacts(&d, NULL, 1u) && !memcmp(&before, &d, sizeof d),
          "null nonempty state rejected");

    for (unsigned i = 0u; i < MTZ2_CONTACT_MAX; i++) {
        contacts[i] = (s5l_mt_contact_t){(uint8_t)(i + 1u), MTZ2_PHASE_MAKE_TOUCH,
            (uint16_t)(20u + 40u * i), 160u, 255u, 8u, 8u};
    }
    CHECK(s5l_mtz1_set_contacts(&d, contacts, MTZ2_CONTACT_MAX), "all five contacts fit CE frame");
    read_frame(&slave, rx);
    CHECK(rx[4] == MTZ2_CONTACT_MAX && rx[50] == MTZ2_PHASE_TOUCHING &&
          rx[18] == MTZ2_PHASE_MAKE_TOUCH && rx[146] == MTZ2_PHASE_MAKE_TOUCH,
          "five-contact frame preserves existing ID");
    CHECK(s5l_mtz1_set_contacts(&d, NULL, 0u), "release five contacts");
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    read_frame(&slave, rx);
    bool all_broken = rx[4] == MTZ2_CONTACT_MAX;
    for (unsigned i = 0u; i < MTZ2_CONTACT_MAX; i++)
        all_broken = all_broken && rx[18u + 32u * i] == MTZ2_PHASE_BREAK_TOUCH &&
                     rx[33u + 32u * i] == 0u;
    CHECK(all_broken && !d.held_n, "all five fingers receive break reports");
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS * 3u);
    CHECK(!d.pending_len && !d.atn, "released gesture remains idle");
}

static unsigned raw16(const uint8_t *p) {
    return p[0] | ((unsigned)p[1] << 8);
}

static void test_motion_velocity(void) {
    s5l_mtz1_t d = {0};
    s5l_spi_slave_t slave;
    uint8_t rx[MTZ2_PAYLOAD_LIMIT + 3u];
    s5l_mtz1_reset(&d);
    s5l_mtz1_bind(&d, &slave);
    d.firmware = true;
    d.uptime_ms = 1000u;
    s5l_mt_contact_t c = {1u, MTZ2_PHASE_MAKE_TOUCH, 80u, 400u, 255u, 8u, 8u};
    CHECK(s5l_mtz1_set_contacts(&d, &c, 1u), "motion test make accepted");
    read_frame(&slave, rx);
    CHECK(raw16(rx + 25u) == 0u && raw16(rx + 27u) == 0u,
          "new finger has no fabricated velocity");
    int x0 = (int16_t)raw16(rx + 21u), y0 = (int16_t)raw16(rx + 23u);
    d.uptime_ms += 100u;
    c.x = 120u; c.y = 300u;
    CHECK(s5l_mtz1_set_contacts(&d, &c, 1u), "motion test move accepted");
    read_frame(&slave, rx);
    int dx = (int16_t)raw16(rx + 21u) - x0;
    int dy = (int16_t)raw16(rx + 23u) - y0;
    CHECK((int16_t)raw16(rx + 25u) == dx * 80 / 100 &&
          (int16_t)raw16(rx + 27u) == dy * 80 / 100 && dx > 0 && dy > 0,
          "right/up movement carries guest velocity in 1/8 mm per second");
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    read_frame(&slave, rx);
    CHECK(raw16(rx + 25u) == 0u && raw16(rx + 27u) == 0u,
          "stationary held report stops motion");
    d.uptime_ms += 1u;
    c.x = 0u; c.y = 479u;
    CHECK(s5l_mtz1_set_contacts(&d, &c, 1u), "fast reverse move accepted");
    read_frame(&slave, rx);
    CHECK((int16_t)raw16(rx + 25u) == INT16_MIN &&
          (int16_t)raw16(rx + 27u) == INT16_MIN,
          "fast reverse velocity saturates without signed wrap");
    CHECK(s5l_mtz1_set_contacts(&d, NULL, 0u), "motion release accepted");
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    read_frame(&slave, rx);
    CHECK(raw16(rx + 25u) == 0u && raw16(rx + 27u) == 0u &&
          rx[18] == MTZ2_PHASE_BREAK_TOUCH,
          "release carries zero velocity and break phase");
}

static void test_os1_coordinates(void) {
    s5l_mtz1_t d = {0};
    uint8_t frame[MTZ2_PAYLOAD_LIMIT];
    s5l_mtz1_reset(&d);
    s5l_mt_contact_t c = {1u, MTZ2_PHASE_MAKE_TOUCH, 0u, 0u, 255u, 8u, 8u};
    /* Independently measured bounds of the loaded 1A543a family-1 grid.
     * Invert the genuine guest normalizer, not a second copy of our encoder. */
    for (unsigned x = 0u; x < 320u; x++) {
        c.x = (uint16_t)x;
        unsigned n = s5l_mt_frame_encode_ce(&d.geom, &c, 1u, 1u, 1000u, frame);
        CHECK(n == 48u && raw16(frame + 20u) * 320u / 4511u == x,
              "OS1 X coordinate %u reaches its own pixel", x);
    }
    for (unsigned y = 0u; y < 480u; y++) {
        c.y = (uint16_t)y;
        unsigned n = s5l_mt_frame_encode_ce(&d.geom, &c, 1u, 1u, 1000u, frame);
        CHECK(n == 48u && (6926u - raw16(frame + 22u)) * 480u / 6926u == y,
              "OS1 Y coordinate %u reaches its own pixel", y);
    }
    static const struct { unsigned x, y, wire_x, wire_y; } samples[] = {
        {0u, 0u, 7u, 6919u}, {319u, 479u, 4503u, 8u},
        {45u, 65u, 641u, 5981u}, {275u, 250u, 3883u, 3312u},
        {192u, 348u, 2713u, 1898u}
    };
    for (unsigned i = 0u; i < sizeof samples / sizeof samples[0]; i++) {
        c.x = (uint16_t)samples[i].x; c.y = (uint16_t)samples[i].y;
        CHECK(s5l_mt_frame_encode_ce(&d.geom, &c, 1u, 1u, 1000u, frame) == 48u &&
              raw16(frame + 20u) == samples[i].wire_x && raw16(frame + 22u) == samples[i].wire_y,
              "OS1 golden wire coordinates at %u,%u", samples[i].x, samples[i].y);
    }
    /* Keep the inherited N82/CC encoding intact at the old golden position. */
    c.x = 48u; c.y = 60u;
    CHECK(s5l_mt_frame_encode(&d.geom, &c, 1u, 1u, 1000u, frame) == 42u &&
          frame[14] == 0u && frame[15] == 0x82u && frame[16] == 2u &&
          frame[18] == 0u && frame[19] == 0xcdu && frame[20] == 0x18u,
          "N82 CC coordinates remain unchanged");
}

int main(void) {
    s5l_mtz1_t d;
    s5l_spi_slave_t s;
    uint8_t tx[1024], rx[1024];
    memset(&d, 0, sizeof d);
    s5l_mtz1_reset(&d);
    s5l_mtz1_bind(&d, &s);
    s5l_mtz1_power_pin(&d, true);
    s5l_mt_contact_t c = { 1u, MTZ2_PHASE_MAKE_TOUCH, 48u, 60u,
                           255u, 8u, 8u };
    CHECK(!s5l_mtz1_set_contacts(&d, &c, 1u),
          "reject contact in bootloader mode");

    memset(tx, 0, sizeof tx);
    tx[0] = 0xc2u;
    tx[1] = 0x40u;
    uint16_t ck = sum(tx, 1022u);
    tx[1022] = (uint8_t)(ck >> 8);
    tx[1023] = (uint8_t)ck;
    txn(&s, tx, rx, sizeof tx);
    uint8_t check[4] = { 5u, 0u, 0u, 6u };
    txn(&s, check, rx, sizeof check);
    CHECK(rx[0] == 0xd0u && rx[1] == 0u &&
          rx[2] == (uint8_t)(ck >> 8) && rx[3] == (uint8_t)ck,
          "A-speed checksum");

    uint8_t c3[4] = { 0xc3u, 0x3eu, 0x3eu, 0x3eu };
    txn(&s, c3, rx, sizeof c3);
    uint8_t raw[3] = { 0x11u, 0x22u, 0x33u };
    txn(&s, raw, rx, sizeof raw);
    txn(&s, check, rx, sizeof check);
    uint8_t c4[4] = { 0xc4u, 0u, 0u, 0xc4u };
    txn(&s, c4, rx, sizeof c4);
    CHECK(d.firmware, "B-speed execute enters firmware");

    uint8_t d0[4] = { 0xd0u, 0u, 0u, 0u };
    txn(&s, d0, rx, sizeof d0);
    CHECK(rx[0] == 0xaau && rx[1] == 1u && rx[2] == 2u && rx[3] == 0u,
          "interface version");

    uint8_t ri[8] = { 0x8fu, 0xd3u, 0u, 0u, 0u, 0u, 0u, 0u };
    txn(&s, ri, rx, sizeof ri);
    uint16_t ric = (uint16_t)(0xd3u + 5u);
    CHECK(rx[0] == 0xaau && rx[4] == 0u && rx[5] == 5u &&
          rx[6] == (uint8_t)(ric >> 8) && rx[7] == (uint8_t)ric,
          "report info");
    uint8_t gr[11] = { 0x82u, 0xd3u };
    txn(&s, gr, rx, sizeof gr);
    CHECK(rx[4] == 1u && rx[5] == 15u && rx[6] == 10u &&
          rx[7] == 1u && rx[8] == 0u &&
          rx[9] == 0u && rx[10] == 0xd3u + 27u, "report body/checksum");

    static const struct {
        uint8_t id, len;
    } params[] = {
        { 0x40u, 1u }, { 0x41u, 1u }, { 0x4fu, 1u }, { 0x68u, 1u },
        { 0x70u, 1u }, { 0xa0u, 1u }, { 0xa3u, 1u }, { 0xa4u, 1u },
        { 0xa5u, 1u }, { 0xa8u, 1u }, { 0xadu, 1u }, { 0xafu, 1u },
        { 0xb0u, 1u }, { 0xd7u, 1u }, { 0xaeu, 2u },
        { 0x7fu, 4u }, { 0xb4u, 4u }, { 0xb6u, 4u },
        { 0xbfu, 4u }, { 0xcbu, 4u }, { 0xb1u, 6u },
        { 0xb2u, 6u }, { 0xabu, 1u }
    };
    uint8_t expected[6];
    for (unsigned i = 0; i < sizeof params / sizeof params[0]; i++) {
        uint8_t info[8] = { 0x8fu, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
        uint8_t set[12] = { 0x92u, 0u };
        uint8_t get[12] = { 0x82u, 0u };
        uint16_t pc;
        info[1] = params[i].id;
        set[1] = params[i].id;
        get[1] = params[i].id;
        for (unsigned j = 0; j < params[i].len; j++) {
            expected[j] = (uint8_t)(i + j + 1u);
            set[4u + j] = expected[j];
        }
        pc = (uint16_t)(params[i].id + sum(expected, params[i].len));
        set[4u + params[i].len] = (uint8_t)(pc >> 8);
        set[5u + params[i].len] = (uint8_t)pc;
        txn(&s, info, rx, sizeof info);
        CHECK(rx[0] == 0xaau && rx[4] == 0u &&
              rx[5] == params[i].len &&
              rx[6] == 0u &&
              rx[7] == params[i].id + params[i].len,
              "parameter report info");
        txn(&s, set, rx, params[i].len + 6u);
        CHECK(rx[0] == 0xaau && rx[params[i].len + 4u] ==
              (uint8_t)(pc >> 8) &&
              rx[params[i].len + 5u] == (uint8_t)pc,
              "parameter set checksum");
        txn(&s, get, rx, params[i].len + 6u);
        CHECK(rx[0] == 0xaau &&
              !memcmp(rx + 4u, expected, params[i].len) &&
              rx[params[i].len + 4u] == (uint8_t)(pc >> 8) &&
              rx[params[i].len + 5u] == (uint8_t)pc,
              "parameter readback");
    }

    uint8_t sr[7] = { 0x92u, 0xabu, 0u, 0u, 1u, 0u, 0u };
    uint16_t src = 0xacu;
    sr[5] = (uint8_t)(src >> 8);
    sr[6] = (uint8_t)src;
    txn(&s, sr, rx, sizeof sr);
    CHECK(d.operating_mode == 1u && rx[5] == sr[5] && rx[6] == sr[6],
          "set operating mode");

    uint8_t lr[8] = { 0x46u, 0x46u, 0x46u, 0x46u,
                      0x46u, 0x46u, 0x46u, 0x46u };
    txn(&s, lr, rx, sizeof lr);
    CHECK(rx[0] == 0xaau && rx[1] == 0u && rx[2] == 0u &&
          rx[3] == 0u && rx[4] == 0u, "idle length");

    s5l_mtz1_tick(&d, MTZ1_TICKS_PER_MS * 1000u);
    CHECK(d.tick_acc == 0u && d.uptime_ms == 1000u,
          "idle ticks do not accumulate and advance uptime");
    CHECK(s5l_mtz1_set_contacts(&d, &c, 1u), "queue contact");
    txn(&s, lr, rx, sizeof lr);
    unsigned l = rx[1] << 8 | rx[2];
    uint16_t lc = (uint16_t)(rx[1] + rx[2]);
    CHECK(l == 50u && rx[3] == (uint8_t)(lc >> 8) &&
          rx[4] == (uint8_t)lc && d.pending_len != 0u && d.atn,
          "contact length/checksum/pending");
    uint8_t data[51] = { 0x47u };
    txn(&s, data, rx, sizeof data);
    uint16_t dc = 0u;
    for (unsigned i = 1; i < 49u; i++) dc = (uint16_t)(dc + rx[i]);
    CHECK(!d.atn, "contact ATN clear");
    CHECK(rx[1] == MTZ1_FRAME_TYPE && rx[4] == 1u &&
          rx[11] == MTZ1_FRAME_HEADER &&
          rx[18] == MTZ2_PHASE_MAKE_TOUCH &&
          rx[21] == 0xabu && rx[22] == 0x02u &&
          rx[23] == 0xa6u && rx[24] == 0x17u &&
          rx[33] == 0xffu && rx[34] == 0u &&
          rx[37] == 0xffu && rx[38] == 0u,
          "CE contact data/phase/density");
    CHECK(rx[49] == (uint8_t)(dc >> 8) && rx[50] == (uint8_t)dc,
          "contact checksum");
    CHECK(frame_ms(rx) == 1000u && d.frame_ms == 1000u,
          "initial touch timestamp uses uptime");

    uint64_t queued = d.frames_queued;
    for (unsigned i = 0; i < 600u; i++) s5l_mtz1_tick(&d, 100u);
    CHECK(d.atn && d.frames_queued == queued + 1u,
          "chunked ticks queue exactly one touching frame");
    txn(&s, lr, rx, sizeof lr);
    CHECK((rx[1] << 8 | rx[2]) == 50u && d.atn,
          "touching frame queued");
    txn(&s, data, rx, sizeof data);
    CHECK(rx[1] == MTZ1_FRAME_TYPE && rx[4] == 1u &&
          rx[18] == MTZ2_PHASE_TOUCHING &&
          rx[33] == 0xffu && rx[37] == 0xffu,
          "touching frame phase");
    CHECK(frame_ms(rx) == 1010u && d.frame_ms == 1010u,
          "touching frame timestamp (frame=%u state=%u)",
          frame_ms(rx), d.frame_ms);

    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    txn(&s, lr, rx, sizeof lr);
    CHECK((rx[1] << 8 | rx[2]) == 50u && d.atn,
          "second touching frame queued");
    txn(&s, data, rx, sizeof data);
    CHECK(rx[18] == MTZ2_PHASE_TOUCHING &&
          frame_ms(rx) == 1020u && d.frame_ms == 1020u,
          "second touching frame (frame=%u state=%u)",
          frame_ms(rx), d.frame_ms);

    CHECK(s5l_mtz1_set_contacts(&d, NULL, 0u), "schedule lift");
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    txn(&s, lr, rx, sizeof lr);
    CHECK((rx[1] << 8 | rx[2]) == 50u && d.atn,
          "break frame queued");
    txn(&s, data, rx, sizeof data);
    CHECK(rx[1] == MTZ1_FRAME_TYPE && rx[4] == 1u &&
          rx[18] == MTZ2_PHASE_BREAK_TOUCH &&
          rx[33] == 0u && rx[37] == 0u,
          "break frame phase/density");
    CHECK(!d.held_n && !d.lift_pending && frame_ms(rx) == 1030u &&
          d.frame_ms == 1030u, "lift state cleared (frame=%u state=%u)",
          frame_ms(rx), d.frame_ms);
    s5l_mtz1_tick(&d, MTZ1_FRAME_TICKS);
    CHECK(!d.atn, "no frame after lift");

    s5l_mtz2_t z2;
    uint8_t a[MTZ2_PAYLOAD_LIMIT], b[MTZ2_PAYLOAD_LIMIT];
    s5l_mtz2_reset(&z2);
    unsigned na = s5l_mtz2_encode(&z2, &c, 1u, 7u, 123u, a);
    s5l_mt_geom_t geom = { z2.family_id, z2.endianness, z2.rows, z2.columns,
                           z2.bcd_version, z2.buttons, z2.surface_width,
                           z2.surface_height };
    unsigned nb = s5l_mt_frame_encode(&geom, &c, 1u, 7u, 123u, b);
    CHECK(na == nb && !memcmp(a, b, na), "MTZ2 shared frame encoding");

    s5l_mtz1_power_pin(&d, false);
    CHECK(!d.firmware && !d.atn, "power off resets");
    test_contact_lifecycle();
    test_os1_coordinates();
    test_motion_velocity();
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
