#include "board.h"
#include "bringup_os1.h"
#include "snapshot.h"

#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static s5l8900_t machine;
static bool machine_ready;
static arm_status_t last_status = ARM_OK;
static char error_text[256];
static uint8_t serial_ring[65536];
static unsigned serial_read_pos;
static unsigned serial_write_pos;
#define EMU_INPUT_HOLD_TICKS 600000u
static bool touch_active;
static bool release_pending;
static uint64_t touch_down_ticks;
static bool button_active[S5L_BUTTON_COUNT];
static bool button_release_pending[S5L_BUTTON_COUNT];
static uint64_t button_down_ticks[S5L_BUTTON_COUNT];

static void set_error(const char *text) {
    snprintf(error_text, sizeof error_text, "%s", text ? text : "unknown error");
}

static void serial_tx(void *ctx, uint8_t byte) {
    (void)ctx;
    unsigned next = (serial_write_pos + 1u) % (unsigned)sizeof serial_ring;
    if (next == serial_read_pos)
        serial_read_pos = (serial_read_pos + 1u) % (unsigned)sizeof serial_ring;
    serial_ring[serial_write_pos] = byte;
    serial_write_pos = next;
}

static void release_machine(void) {
    if (machine_ready) {
        s5l8900_free(&machine);
        machine_ready = false;
    }
}

static void release_touch_now(void) {
    if (s5l8900_set_touch(&machine, NULL, 0u)) {
        touch_active = false;
        release_pending = false;
    } else {
        set_error("touch release failed");
    }
}

static void release_button_now(unsigned which) {
    if (s5l8900_set_button(&machine, which, false)) {
        button_active[which] = false;
        button_release_pending[which] = false;
    } else {
        set_error("button release failed");
    }
}

static void flush_pending_inputs(void) {
    if (release_pending &&
        machine.timer.ticks - touch_down_ticks >= EMU_INPUT_HOLD_TICKS)
        release_touch_now();
    for (unsigned which = 0u; which < S5L_BUTTON_COUNT; which++) {
        if (button_release_pending[which] &&
            machine.timer.ticks - button_down_ticks[which] >=
                EMU_INPUT_HOLD_TICKS)
            release_button_now(which);
    }
}

EMSCRIPTEN_KEEPALIVE
const char *emu_error(void) {
    return error_text;
}

EMSCRIPTEN_KEEPALIVE
int emu_init(const uint8_t *kernel, size_t kernel_size,
             const uint8_t *devicetree, size_t devicetree_size,
             const uint8_t *rootfs, size_t rootfs_size, unsigned ram_mb) {
    static const char *const unmatch[] = {"arm-io/usb-otg"};
    const s5l_board_t *board;
    s5l_os1_request_t request;
    s5l_os1_result_t result;

    set_error("");
    release_machine();
    serial_read_pos = serial_write_pos = 0u;
    last_status = ARM_OK;
    touch_active = false;
    release_pending = false;
    memset(button_active, 0, sizeof button_active);
    memset(button_release_pending, 0, sizeof button_release_pending);
    if (!kernel || !kernel_size || !devicetree || !devicetree_size) {
        set_error("kernel and devicetree are required");
        return -1;
    }
    board = s5l_board_find("m68");
    if (!board) {
        set_error("m68 board is unavailable");
        return -2;
    }
    if (!ram_mb)
        ram_mb = 256u;
    if (!s5l_board_init(board, &machine, ram_mb << 20u)) {
        set_error("machine initialization failed");
        return -3;
    }
    machine_ready = true;
    request.kernel = kernel;
    request.kernel_size = kernel_size;
    request.devicetree = devicetree;
    request.devicetree_size = devicetree_size;
    request.ramdisk = rootfs;
    request.ramdisk_size = rootfs_size;
    request.cmdline = "debug=0x8 serial=3 rd=md0 -v";
    request.framebuffer = true;
    request.v_display = 0u;
    request.fb_width = board->lcd_width;
    request.fb_height = board->lcd_height;
    request.debug_enabled = true;
    request.unmatch = unmatch;
    request.unmatch_count = sizeof unmatch / sizeof unmatch[0];
    if (!s5l_os1_boot(&machine, &request, &result)) {
        set_error(result.detail[0] ? result.detail : "OS1 boot setup failed");
        release_machine();
        return -4;
    }
    if (!s5l8900_set_uart0_host(&machine, serial_tx, NULL)) {
        set_error("UART0 host tap setup failed");
        release_machine();
        return -5;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_load_snapshot(const uint8_t *buf, size_t len) {
    snapshot_status_t status;
    if (!machine_ready) {
        set_error("machine is not initialized");
        return -1;
    }
    if (!buf || !len) {
        set_error("snapshot buffer is empty");
        return -2;
    }
    status = snapshot_load_mem(&machine, buf, len);
    if (status != SNAP_OK) {
        set_error(snapshot_strerror(status));
        return -3;
    }
    touch_active = false;
    release_pending = false;
    memset(button_active, 0, sizeof button_active);
    memset(button_release_pending, 0, sizeof button_release_pending);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
unsigned emu_run(unsigned max_steps) {
    unsigned total = 0u;
    if (!machine_ready) {
        set_error("machine is not initialized");
        last_status = ARM_HALT;
        return 0u;
    }
    while (total < max_steps) {
        unsigned slice = max_steps - total;
        unsigned ran;
        if (slice > 200000u)
            slice = 200000u;
        flush_pending_inputs();
        ran = s5l8900_run(&machine, slice, &last_status);
        total += ran;
        flush_pending_inputs();
        if (!ran)
            break;
    }
    flush_pending_inputs();
    return total;
}

EMSCRIPTEN_KEEPALIVE
int emu_status(void) {
    return (int)last_status;
}

EMSCRIPTEN_KEEPALIVE
int emu_frame(uint8_t *rgba_out, unsigned *width, unsigned *height) {
    static uint8_t rgb[320u * 480u * 3u];
    unsigned w = 0u, h = 0u;
    uint32_t window;
    if (!machine_ready || !rgba_out || !width || !height)
        return -1;
    window = s5l_clcd_active_window(&machine.clcd);
    if (window == CLCD_WIN_NONE)
        return 0;
    if (!s5l_clcd_scanout(&machine.clcd, window, machine.ram,
                          machine.ram_base, machine.ram_size, rgb, sizeof rgb,
                          &w, &h) || w > 320u || h > 480u)
        return -2;
    for (unsigned i = 0u; i < w * h; i++) {
        rgba_out[4u * i] = rgb[3u * i];
        rgba_out[4u * i + 1u] = rgb[3u * i + 1u];
        rgba_out[4u * i + 2u] = rgb[3u * i + 2u];
        rgba_out[4u * i + 3u] = 255u;
    }
    *width = w;
    *height = h;
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int emu_touch(int down, unsigned x, unsigned y) {
    s5l_mt_contact_t contact;
    if (!machine_ready)
        return -1;
    if (down == 0) {
        if (!touch_active)
            return 0;
        else if (machine.timer.ticks - touch_down_ticks <
                 EMU_INPUT_HOLD_TICKS) {
            release_pending = true;
            return 0;
        } else {
            release_touch_now();
            return release_pending ? -2 : 0;
        }
    }
    if (down != 1 && down != 2)
        return -2;
    if (down == 1 && release_pending) {
        release_touch_now();
        if (release_pending)
            return -2;
    }
    memset(&contact, 0, sizeof contact);
    contact.id = 1u;
    contact.phase = down == 1 ? MTZ2_PHASE_MAKE_TOUCH : MTZ2_PHASE_TOUCHING;
    contact.x = (uint16_t)x;
    contact.y = (uint16_t)y;
    contact.pressure = 255u;
    contact.major = 8u;
    contact.minor = 8u;
    if (!s5l8900_set_touch(&machine, &contact, 1u))
        return -2;
    touch_active = true;
    if (down == 1)
        touch_down_ticks = machine.timer.ticks;
    release_pending = false;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_button(unsigned which, int pressed) {
    if (!machine_ready)
        return -1;
    if (which >= S5L_BUTTON_COUNT)
        return -2;
    if (pressed) {
        if (button_release_pending[which]) {
            release_button_now(which);
            if (button_release_pending[which])
                return -3;
        }
        if (!s5l8900_set_button(&machine, which, true))
            return -3;
        if (!button_active[which])
            button_down_ticks[which] = machine.timer.ticks;
        button_active[which] = true;
        button_release_pending[which] = false;
        return 0;
    }
    if (!button_active[which])
        return s5l8900_set_button(&machine, which, false) ? 0 : -3;
    if (machine.timer.ticks - button_down_ticks[which] <
        EMU_INPUT_HOLD_TICKS) {
        button_release_pending[which] = true;
        return 0;
    }
    release_button_now(which);
    return button_release_pending[which] ? -3 : 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_set_cpi(unsigned cpi) {
    if (!machine_ready || !s5l8900_set_cycles_per_insn(&machine, cpi))
        return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
uint64_t emu_cycles(void) {
    return machine_ready ? machine.cpu.cycles : 0u;
}

EMSCRIPTEN_KEEPALIVE
int emu_serial_read(uint8_t *out, unsigned max) {
    unsigned count = 0u;
    if (!out || !machine_ready)
        return 0;
    while (count < max && serial_read_pos != serial_write_pos) {
        out[count++] = serial_ring[serial_read_pos];
        serial_read_pos = (serial_read_pos + 1u) % (unsigned)sizeof serial_ring;
    }
    return (int)count;
}
