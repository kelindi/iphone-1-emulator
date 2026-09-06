/* Exercise the real browser API against an M68 machine. Including its source
 * permits a deterministic fake guest to consume SPI reports and acknowledge
 * GPIO interrupts without needing firmware or waiting for an OS boot. */
#include "../emu_api.c"

static unsigned checks;
#define CHECK(expression) do { \
    checks++; \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s (API error: %s)\n", \
                __FILE__, __LINE__, #expression, emu_error()); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void read_touch(void) {
    s5l_spi_slave_t *slave = &machine.spi[2].slaves[0];
    unsigned count = machine.mtz1.pending_len + 3u;
    s5l_mtz1_select_pin(slave->ctx, false);
    for (unsigned i = 0u; i < count; i++)
        slave->transfer(slave->ctx, i ? 0u : 0x47u);
    s5l_mtz1_select_pin(slave->ctx, true);
}

static void advance_ticks(unsigned ticks) {
    /* s5l8900_tick accepts CPU cycles, whereas these tests name timebase ticks. */
    machine.timer.ticks += ticks;
    s5l_mtz1_tick(&machine.mtz1, ticks);
}

static void test_gesture_queue(void) {
    uint32_t contacts[12] = {1u, 40u, 60u, 255u, 8u, 8u,
                             2u, 200u, 300u, 255u, 8u, 8u};
    CHECK(emu_touch_contacts(contacts, 1u) == 0);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_MAKE_TOUCH);
    unsigned baseline = touch_queue_count;
    contacts[0] = 6u;
    CHECK(emu_touch_contacts(contacts, 1u) == -2 && touch_queue_count == baseline);
    contacts[0] = 1u;
    contacts[6] = 1u;
    CHECK(emu_touch_contacts(contacts, 2u) == -2 && touch_queue_count == baseline);
    contacts[6] = 2u;
    CHECK(emu_touch_contacts(NULL, 1u) == -2);
    CHECK(emu_touch_contacts(contacts, 6u) == -2);

    for (unsigned i = 0u; i < 10000u; i++) {
        contacts[1] = 50u + i % 200u;
        CHECK(emu_touch_contacts(contacts, 1u) == 0);
    }
    CHECK(touch_queue_count == 1u);
    CHECK(emu_touch_contacts(contacts, 2u) == 0);
    contacts[7] = 210u;
    CHECK(emu_touch_contacts(contacts, 2u) == 0);
    CHECK(emu_touch_contacts(NULL, 0u) == 0);
    CHECK(touch_queue_count == 4u && emu_inputs_idle() == 0);

    read_touch();
    flush_pending_inputs();
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_TOUCHING);
    read_touch();
    flush_pending_inputs();
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_TOUCHING);
    CHECK(machine.mtz1.pending[49] == MTZ2_PHASE_MAKE_TOUCH);
    read_touch();
    flush_pending_inputs();
    CHECK(machine.mtz1.pending[49] == MTZ2_PHASE_TOUCHING);
    read_touch();
    flush_pending_inputs();
    CHECK(touch_queue_count == 1u && machine.mtz1.held_n == 2u);
    advance_ticks(EMU_TOUCH_HOLD_TICKS);
    read_touch();
    flush_pending_inputs();
    CHECK(touch_queue_count == 0u && machine.mtz1.lift_pending);
    CHECK(emu_inputs_idle() == 0);
    advance_ticks(MTZ1_FRAME_TICKS);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_BREAK_TOUCH);
    CHECK(machine.mtz1.pending[49] == MTZ2_PHASE_BREAK_TOUCH);
    CHECK(emu_inputs_idle() == 0);
    read_touch();
    CHECK(!machine.mtz1.held_n && emu_inputs_idle() == 1);
}


static void test_delayed_partial_and_repeat(void) {
    uint32_t contacts[] = {1, 40, 60, 255, 8, 8, 5, 180, 200, 255, 8, 8};
    CHECK(emu_touch_contacts(contacts, 2u) == 0);
    CHECK(emu_touch_contacts(contacts + 6u, 1u) == 0);
    advance_ticks(EMU_TOUCH_HOLD_TICKS * 10u);
    flush_pending_inputs();
    CHECK(touch_queue_count == 1u);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_MAKE_TOUCH);
    CHECK(machine.mtz1.pending[49] == MTZ2_PHASE_MAKE_TOUCH);
    read_touch();
    flush_pending_inputs();
    CHECK(touch_queue_count == 1u);
    advance_ticks(MTZ1_FRAME_TICKS);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_TOUCHING);
    CHECK(machine.mtz1.pending[49] == MTZ2_PHASE_TOUCHING);
    flush_pending_inputs();
    CHECK(touch_queue_count == 1u); /* full-set update preserves unread report */
    read_touch();
    flush_pending_inputs();
    CHECK(touch_queue_count == 0u && machine.mtz1.held_n == 1u);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_BREAK_TOUCH);
    CHECK(machine.mtz1.pending[49] == MTZ2_PHASE_TOUCHING);
    read_touch();
    CHECK(emu_touch_contacts(NULL, 0u) == 0);
    advance_ticks(MTZ1_FRAME_TICKS);
    CHECK(machine.mtz1.pending[16] == 5u);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_BREAK_TOUCH);
    read_touch();
    CHECK(emu_inputs_idle() == 1);

    contacts[0] = 4u;
    for (unsigned i = 0u; i < 2u; i++) {
        CHECK(emu_touch_contacts(contacts, 1u) == 0);
        CHECK(emu_touch_contacts(NULL, 0u) == 0);
    }
    unsigned phases[6] = {MTZ2_PHASE_MAKE_TOUCH, MTZ2_PHASE_TOUCHING,
        MTZ2_PHASE_BREAK_TOUCH, MTZ2_PHASE_MAKE_TOUCH,
        MTZ2_PHASE_TOUCHING, MTZ2_PHASE_BREAK_TOUCH};
    unsigned seen = 0u;
    for (unsigned i = 0u; i < 20u && !emu_inputs_idle(); i++) {
        advance_ticks(EMU_TOUCH_HOLD_TICKS);
        flush_pending_inputs();
        if (machine.mtz1.pending_len) {
            CHECK(seen < 6u);
            CHECK(machine.mtz1.pending[16] == 4u);
            CHECK(machine.mtz1.pending[17] == phases[seen++]);
            read_touch();
        }
        flush_pending_inputs();
    }
    CHECK(seen == 6u && emu_inputs_idle() == 1);
}

static void test_hold_floors(void) {
    /* A quick host click must retain its MAKE even if the guest has not read
     * it by the release deadline. Only the actual sensor frame can break it. */
    CHECK(emu_touch(1, 40u, 60u) == 0);
    CHECK(emu_touch(0, 40u, 60u) == 0);
    advance_ticks(EMU_TOUCH_HOLD_TICKS - 1u);
    flush_pending_inputs();
    CHECK(touch_queue_count == 1u && !machine.mtz1.lift_pending);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_MAKE_TOUCH);
    advance_ticks(1u);
    flush_pending_inputs();
    CHECK(touch_queue_count == 1u && !machine.mtz1.lift_pending);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_MAKE_TOUCH);
    read_touch();
    flush_pending_inputs();
    CHECK(touch_queue_count == 1u && !machine.mtz1.lift_pending);
    advance_ticks(MTZ1_FRAME_TICKS);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_TOUCHING);
    flush_pending_inputs();
    CHECK(touch_queue_count == 0u && machine.mtz1.lift_pending);
    advance_ticks(MTZ1_FRAME_TICKS * 10u);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_TOUCHING);
    read_touch();
    advance_ticks(MTZ1_FRAME_TICKS);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_BREAK_TOUCH);
    read_touch();
    CHECK(emu_inputs_idle() == 1);

    /* Physical-button debounce keeps its longer floor even after the guest
     * has acknowledged the down edge. The eventual up edge is still real. */
    const unsigned mask = 1u << 8;
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTTYPE + 4u, mask);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTLEVEL + 4u, mask);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTSTAT + 4u, mask);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTEN + 4u, mask);
    CHECK(emu_button(S5L_BUTTON_MENU, 1) == 0);
    CHECK(emu_button(S5L_BUTTON_MENU, 0) == 0);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTLEVEL + 4u, 0u);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTSTAT + 4u, mask);
    advance_ticks(EMU_TOUCH_HOLD_TICKS);
    flush_pending_inputs();
    CHECK(emu_button_state(S5L_BUTTON_MENU) == 1);
    advance_ticks(EMU_BUTTON_HOLD_TICKS - EMU_TOUCH_HOLD_TICKS - 1u);
    flush_pending_inputs();
    CHECK(emu_button_state(S5L_BUTTON_MENU) == 1);
    advance_ticks(1u);
    flush_pending_inputs();
    CHECK(emu_button_state(S5L_BUTTON_MENU) == 0);
    CHECK(s5l_gpioic_pending(&machine.gpioic, s5l_button_line(S5L_BUTTON_MENU)));
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTLEVEL + 4u, mask);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTSTAT + 4u, mask);
    CHECK(emu_inputs_idle() == 1);
}

static void test_saturation_and_cancel(void) {
    const uint32_t contact[6] = {1u, 40u, 60u, 255u, 8u, 8u};
    CHECK(emu_touch_contacts(contact, 1u) == 0);
    for (unsigned i = 0u; i < EMU_TOUCH_QUEUE_CAPACITY; i++)
        CHECK(emu_touch_contacts(i % 2u ? contact : NULL, i % 2u ? 1u : 0u) == 0);
    CHECK(emu_touch_contacts(NULL, 0u) == -3);
    CHECK(touch_queue_count == EMU_TOUCH_QUEUE_CAPACITY);
    /* Saturation preserves transitions. Cancellation drops only host events
     * that have not reached the device and releases the actual guest finger. */
    CHECK(emu_release_inputs() == 0 && touch_queue_count == 0u);
    CHECK(machine.mtz1.lift_pending && emu_inputs_idle() == 0);
    read_touch();
    advance_ticks(MTZ1_FRAME_TICKS);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_BREAK_TOUCH);
    read_touch();
    CHECK(emu_inputs_idle() == 1);
}

static void test_checkpoint_input_lifetime(void) {
    CHECK(emu_touch(1, 40u, 60u) == 0);
    s5l_mtz1_t saved_touch = machine.mtz1;
    uint8_t *snapshot = emu_save_snapshot();
    CHECK(snapshot != NULL);
    size_t size = emu_snapshot_size();
    CHECK(size > machine.ram_size);
    CHECK(!memcmp(&saved_touch, &machine.mtz1, sizeof saved_touch));
    CHECK(emu_touch(2, 60u, 80u) == 0 && touch_queue_count == 1u);
    /* Failed imports leave both guest state and the current host gesture
     * alone. Only a successfully restored machine releases stale inputs. */
    snapshot[0] ^= 1u;
    CHECK(emu_load_snapshot(snapshot, size) == -3);
    CHECK(!memcmp(&saved_touch, &machine.mtz1, sizeof saved_touch));
    CHECK(touch_queue_count == 1u);
    snapshot[0] ^= 1u;
    CHECK(emu_load_snapshot(snapshot, size) == 0);
    CHECK(machine.mtz1.lift_pending && touch_queue_count == 0u);
    read_touch();
    advance_ticks(MTZ1_FRAME_TICKS);
    CHECK(machine.mtz1.pending[17] == MTZ2_PHASE_BREAK_TOUCH);
    read_touch();
    CHECK(!machine.mtz1.held_n && emu_inputs_idle() == 1);
    CHECK(emu_touch(0, 0u, 0u) == 0);
    emu_free_snapshot();
    emu_free_snapshot();
    CHECK(emu_snapshot_size() == 0u);
}

static void test_button_release_and_ringer(void) {
    /* The guest auto-flips level-sensitive interrupts before acknowledging
     * them. A lost-focus release must survive a busy down interrupt. */
    const unsigned mask = (1u << 8) | (1u << 11);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTTYPE + 4u, mask);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTLEVEL + 4u, mask);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTSTAT + 4u, mask);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTEN + 4u, mask);
    CHECK(emu_button(S5L_BUTTON_RINGERAB, 1) == 0);
    CHECK(emu_button(S5L_BUTTON_MENU, 1) == 0);
    CHECK(emu_inputs_idle() == 0 && emu_button_state(S5L_BUTTON_MENU) == 1);
    CHECK(emu_release_inputs() == 0 && button_release_pending[S5L_BUTTON_MENU]);
    CHECK(emu_button_state(S5L_BUTTON_RINGERAB) == 1);
    unsigned levels = s5l_gpioic_read(&machine.gpioic, GPIOIC_INTLEVEL + 4u);
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTLEVEL + 4u, levels ^ (1u << 8));
    s5l_gpioic_write(&machine.gpioic, GPIOIC_INTSTAT + 4u, 1u << 8);
    flush_pending_inputs();
    CHECK(emu_button_state(S5L_BUTTON_MENU) == 0 && emu_inputs_idle() == 1);
    CHECK(emu_button_state(S5L_BUTTON_RINGERAB) == 1);
    uint8_t *snapshot = emu_save_snapshot();
    CHECK(snapshot != NULL);
    CHECK(emu_load_snapshot(snapshot, emu_snapshot_size()) == 0);
    CHECK(emu_button_state(S5L_BUTTON_RINGERAB) == 1 && emu_inputs_idle() == 1);
    emu_free_snapshot();
}

static void test_incremental_snapshot_lifetime(void) {
    const uint8_t *original = emu_save_snapshot();
    size_t size = emu_snapshot_size();
    CHECK(original && size);
    uint8_t *expected = malloc(size);
    CHECK(expected != NULL);
    memcpy(expected, original, size);
    CHECK(emu_snapshot_begin() == 0);
    CHECK(emu_snapshot_data() == NULL && emu_snapshot_size() == 0);
    CHECK(emu_snapshot_advance(1) == 0);
    while (!emu_snapshot_advance(1048576)) {}
    CHECK(emu_snapshot_data() != NULL && emu_snapshot_size() == size);
    CHECK(memcmp(expected, emu_snapshot_data(), size) == 0);
    emu_free_snapshot();
    CHECK(emu_snapshot_data() == NULL && emu_snapshot_size() == 0);
    CHECK(emu_snapshot_advance(1048576) == -1);
    CHECK(emu_snapshot_begin() == 0);
    CHECK(emu_save_snapshot() != NULL); /* blocking capture cancels old job */
    CHECK(emu_snapshot_advance(1) == -1);
    emu_free_snapshot();
    free(expected);
}

static void fixture_u32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8u * i));
}
static size_t fixture_property(uint8_t *tree, size_t off, const char *key, const char *value) {
    size_t len = strlen(value) + 1u;
    strcpy((char *)tree + off, key);
    fixture_u32(tree + off + 32u, (uint32_t)len);
    memcpy(tree + off + 36u, value, len);
    return off + 36u + ((len + 3u) & ~(size_t)3u);
}
static void fixture_matched_boot_tree(void) {
    uint8_t *tree = machine.ram + 4096u, *args = machine.ram + 8192u;
    memset(tree, 0, 4096u + 56u);
    fixture_u32(tree, 1); fixture_u32(tree + 4, 1);
    size_t off = fixture_property(tree, 8, "name", "/");
    fixture_u32(tree + off, 1); fixture_u32(tree + off + 4, 1); off += 8;
    off = fixture_property(tree, off, "name", "arm-io");
    fixture_u32(tree + off, 2); off += 8;
    off = fixture_property(tree, off, "name", "usb-otg");
    off = fixture_property(tree, off, "compatible", "usb-otg,s5l8900x");
    fixture_u32(args, 0x00010001u);
    fixture_u32(args + 4, 0xc0000000u);
    fixture_u32(args + 8, machine.ram_base);
    fixture_u32(args + 12, machine.ram_size);
    fixture_u32(args + 48, 0xc0001000u);
    fixture_u32(args + 52, (uint32_t)off);
    browser_clock_compatible = browser_guest_clock_compatible();
}
static void test_clock_guest_compatibility(void) {
    release_machine();
    CHECK(s5l_board_init(s5l_board_find("m68"), &machine, 16u << 20));
    machine_ready = true;
    CHECK(!browser_guest_clock_compatible());
    CHECK(emu_host_clock(1) == 0 && !machine.active_host_now);
    fixture_matched_boot_tree();
    CHECK(browser_clock_compatible);
    CHECK(emu_host_clock(1) == 0 && machine.active_host_now);
    CHECK(emu_host_clock(0) == 0);
    uint8_t *args = machine.ram + 8192u;
    uint32_t len = browser_le32(args + 52);
    // The old --unmatch workaround changes only the first compatible byte.
    machine.ram[4096u + len - 16u] = 'x';
    CHECK(!browser_guest_clock_compatible());
    const uint8_t *snapshot = emu_save_snapshot();
    CHECK(snapshot != NULL);
    CHECK(emu_load_snapshot(snapshot, emu_snapshot_size()) == 0);
    CHECK(!browser_clock_compatible);
    CHECK(emu_host_clock(1) == 0 && !machine.active_host_now);
    emu_free_snapshot();
    fixture_matched_boot_tree();
    snapshot = emu_save_snapshot();
    CHECK(snapshot != NULL);
    browser_clock_compatible = false;
    CHECK(emu_load_snapshot(snapshot, emu_snapshot_size()) == 0);
    CHECK(browser_clock_compatible);
    emu_free_snapshot();
    // Invalid pointers, sizes, tree structure and mismatched layout fail closed.
    fixture_u32(args + 48, 0xbffff000u);
    CHECK(!browser_guest_clock_compatible());
    fixture_u32(args + 48, 0xfffff000u);
    CHECK(!browser_guest_clock_compatible());
    fixture_u32(args + 48, 0xc0001001u);
    CHECK(!browser_guest_clock_compatible());
    fixture_u32(args + 48, 0xc0001000u);
    fixture_u32(args + 52, 0xffffffffu);
    CHECK(!browser_guest_clock_compatible());
    fixture_u32(args + 52, len - 1u);
    CHECK(!browser_guest_clock_compatible());
    fixture_u32(args + 52, len);
    fixture_u32(machine.ram + 4096u, 0xffffffffu);
    CHECK(!browser_guest_clock_compatible());
    fixture_matched_boot_tree();
    fixture_u32(args + 12, machine.ram_size + 4096u);
    CHECK(!browser_guest_clock_compatible());
    release_machine();
    CHECK(!browser_clock_compatible);
}

static void fresh_clock_machine(void) {
    release_machine();
    CHECK(s5l_board_init(s5l_board_find("m68"), &machine, 16u << 20));
    machine_ready = true;
    fixture_matched_boot_tree();
    CHECK(browser_clock_compatible);
    machine.cpu_hz = machine.tb_hz = 1000u;
    machine.cpu.r[15] = machine.ram_base;
    machine.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
}

static void test_host_clock_and_cooperative_wait(void) {
    fresh_clock_machine();
    const uint32_t spin = 0xeafffffeu;
    s5l8900_load(&machine, machine.ram_base, &spin, sizeof spin);
    CHECK(!machine.active_host_now && !machine.wfi_host_sleep);
    emscripten_test_now_ms = 1000.0;
    CHECK(emu_host_clock(1) == 0 && emu_clock_delay() == 0);
    uint64_t ticks = machine.timer.ticks;
    CHECK(emu_run(100u) == 100u && machine.timer.ticks == ticks);
    emscripten_test_now_ms += 5.0;
    CHECK(emu_run(100u) == 100u && machine.timer.ticks == ticks + 5u);
    ticks = machine.timer.ticks;
    // Reinstalling on resume discards the paused minute, preserving guest time.
    CHECK(emu_host_clock(0) == 0);
    emscripten_test_now_ms += 60000.0;
    CHECK(emu_host_clock(1) == 0);
    CHECK(emu_run(100u) == 100u && machine.timer.ticks == ticks);
    // A trusted foreground stall is retained, but each synchronization still
    // pays at most 8ms. This 100-instruction call samples at entry and exit.
    emscripten_test_now_ms += 60000.0;
    CHECK(emu_run(100u) == 100u && machine.timer.ticks == ticks + 16u);
    CHECK(machine.active_clock_clamps == 2u && machine.active_clock_debt_ticks == 59984u);
    CHECK(emu_run(100u) == 100u && machine.timer.ticks == ticks + 32u);
    CHECK(machine.active_clock_debt_ticks == 59968u);
    CHECK(emu_host_clock(0) == 0 && !machine.active_host_now && !machine.wfi_host_sleep);
    CHECK(!machine.active_clock_debt_ticks && !machine.active_clock_carry);
    ticks = machine.timer.ticks;
    CHECK(emu_run(100u) == 100u && machine.timer.ticks == ticks + 100u);

    fresh_clock_machine();
    const uint32_t program[] = {0xee070f90u, 0xeafffffdu}; // WFI; branch to WFI.
    s5l8900_load(&machine, machine.ram_base, program, sizeof program);
    machine.timer.t4_count = machine.timer.t4_value = 20u;
    machine.timer.t4_state = TIMER4_STATE_START;
    machine.vic[0].enable = 1u << S5L8900_IRQ_TIMER;
    CHECK(emu_host_clock(1) == 0);
    ticks = machine.timer.ticks;
    // A declined wait must return after the actual WFI, without future ticks.
    CHECK(emu_run(100000u) == 1u);
    CHECK(machine.timer.ticks == ticks && emu_clock_delay() == 8.0);
    CHECK(machine.wfi_paced_failures == 1u); // The synchronous wait was declined.
    emscripten_test_now_ms += 8.0;
    CHECK(emu_clock_delay() == 0);
    CHECK(emu_run(1u) == 1u && machine.timer.ticks == ticks + 8u);
    CHECK(emu_run(100000u) == 1u && emu_clock_delay() == 8.0);
    CHECK(emu_host_clock(0) == 0 && emu_clock_delay() == 0);
}

int main(void) {
    CHECK(emu_host_clock(1) == -1);
    CHECK(emu_touch(1, 40u, 60u) == -1);
    CHECK(emu_release_inputs() == -1);
    CHECK(emu_save_snapshot() == NULL && emu_snapshot_size() == 0u);
    CHECK(emu_snapshot_begin() == -1 && emu_snapshot_data() == NULL);
    CHECK(s5l_board_init(s5l_board_find("m68"), &machine, 16u << 20));
    machine_ready = true;
    machine.mtz1.firmware = true;
    machine.mtz1.powered = true;
    advance_ticks(6000000u);
    test_gesture_queue();
    test_hold_floors();
    test_delayed_partial_and_repeat();
    test_saturation_and_cancel();
    test_checkpoint_input_lifetime();
    test_button_release_and_ringer();
    test_incremental_snapshot_lifetime();
    test_clock_guest_compatibility();
    test_host_clock_and_cooperative_wait();
    release_machine();
    printf("web API: %u checks passed\n", checks);
    return EXIT_SUCCESS;
}
