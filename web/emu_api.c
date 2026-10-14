#include "board.h"
#include "bringup_os1.h"
#include "snapshot.h"
#include "devicetree.h"
#ifdef S5LBOX_WASM_JIT
#include "jit-runtime.h"
#endif

#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static s5l8900_t machine;
static bool machine_ready;
static arm_status_t last_status = ARM_OK;
static char error_text[256];
static uint8_t serial_ring[65536];
static unsigned serial_read_pos;
static unsigned serial_write_pos;
/* Genuine-guest keyboard/navigation checks support a 20 ms touch floor.
 * Momentary buttons retain 100 ms: their debounce path is independent.
 * Neither floor bypasses pending Z1 reports or GPIO edge consumption. */
#define EMU_TOUCH_HOLD_TICKS 120000u
#define EMU_BUTTON_HOLD_TICKS 600000u
#define EMU_TOUCH_QUEUE_CAPACITY 64u
/* Host queue owns transitions until the controller can accept them. Motion
 * may coalesce, but a make/break boundary never does. IDs are panel slots 1..5. */
typedef struct {
    s5l_mt_contact_t contacts[MTZ2_CONTACT_MAX];
    unsigned count;
    bool motion;
} touch_state_t;
static touch_state_t touch_queue[EMU_TOUCH_QUEUE_CAPACITY];
static unsigned touch_queue_head, touch_queue_count;
static touch_state_t requested_touch, applied_touch;
static uint64_t contact_down_ticks[MTZ2_CONTACT_MAX + 1u];
static bool button_active[S5L_BUTTON_COUNT];
static bool button_release_pending[S5L_BUTTON_COUNT];
static bool button_force_release[S5L_BUTTON_COUNT];
static uint64_t button_down_ticks[S5L_BUTTON_COUNT];
static uint8_t *saved_snapshot;
static size_t saved_snapshot_size;
static snapshot_capture_t *snapshot_capture;

EMSCRIPTEN_KEEPALIVE int emu_release_inputs(void);

/* Old saved phones suppressed USB matching, so lockdownd repeatedly exits.
 * Their connection retries must retain the previous clock policy. Identify the
 * actual retained boot tree, not storage metadata: raw exports can be imported
 * into another profile. Unknown layouts also retain deterministic timing.
 * This is a read-only, once-per-load check of our OS1 bringup layout. */
static bool browser_clock_compatible;
static uint32_t browser_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u |
           (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
static bool browser_guest_clock_compatible(void) {
    const uint32_t virt_base = UINT32_C(0xc0000000);
    if (!machine.ram || machine.ram_size < 4096u) return false;
    for (size_t off = 0; off <= machine.ram_size - 56u; off += 4096u) {
        const uint8_t *args = machine.ram + off;
        if (browser_le32(args) != 0x00010001u ||
            browser_le32(args + 4) != virt_base ||
            browser_le32(args + 8) != machine.ram_base ||
            browser_le32(args + 12) != machine.ram_size) continue;
        uint32_t va = browser_le32(args + 48), len = browser_le32(args + 52);
        if (va < virt_base || !len || len > 1048576u) continue;
        size_t tree_off = va - virt_base;
        if ((tree_off & 4095u) || tree_off > off || len > off - tree_off ||
            ((tree_off + len + 4095u) & ~(size_t)4095u) != off) continue;
        dt_t tree;
        dt_node_t root, usb;
        const uint8_t *compatible;
        uint32_t compatible_len;
        static const char expected[] = "usb-otg,s5l8900x";
        if (dt_parse(machine.ram + tree_off, len, &tree, &root) != DT_OK ||
            dt_path(&tree, &root, "arm-io/usb-otg", &usb) != DT_OK ||
            dt_property(&tree, &usb, "compatible", &compatible, &compatible_len) != DT_OK)
            continue;
        return compatible_len >= sizeof expected &&
               memcmp(compatible, expected, sizeof expected) == 0;
    }
    return false;
}

/* Interactive pacing: the synchronous core wait is explicitly declined; the
 * worker awaits its real deadline, then monotonic active time accounts for it.
 * Declined-wait counters therefore count expected cooperative deferrals. */
static double browser_wait_until_ms;
static bool browser_host_now(void *ctx, uint64_t *ns) {
    (void)ctx;
    *ns = (uint64_t)(emscripten_get_now() * 1000000.0);
    return true;
}
static bool browser_defer_wait(void *ctx, uint64_t ns) {
    (void)ctx; browser_wait_until_ms = emscripten_get_now() + (double)ns / 1000000.0;
    return false; /* No sleep has happened; advance no WFI time here. */
}
EMSCRIPTEN_KEEPALIVE int emu_host_clock(int enabled) {
    if (!machine_ready) return -1;
    enabled = enabled && browser_clock_compatible;
    if (enabled && machine.cpu_hz < 125u) return -1;
    browser_wait_until_ms = 0;
    return s5l8900_set_active_host_clock(&machine, enabled ? browser_host_now : NULL, NULL) &&
           s5l8900_set_wfi_host_pacing(&machine, enabled ? browser_defer_wait : NULL, NULL) &&
           s5l8900_set_active_clock_carry(&machine, enabled) ? 0 : -1;
}
EMSCRIPTEN_KEEPALIVE double emu_clock_delay(void) {
    double remaining = browser_wait_until_ms - emscripten_get_now();
    return remaining > 0 ? remaining : 0;
}

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

static void reset_host_inputs(void) {
    touch_queue_head = touch_queue_count = 0u;
    memset(&requested_touch, 0, sizeof requested_touch);
    memset(&applied_touch, 0, sizeof applied_touch);
    memset(contact_down_ticks, 0, sizeof contact_down_ticks);
    memset(button_active, 0, sizeof button_active);
    memset(button_release_pending, 0, sizeof button_release_pending);
    memset(button_force_release, 0, sizeof button_force_release);
}

static void release_machine(void) {
#ifdef S5LBOX_WASM_JIT
    web_jit_bind(NULL);
#endif
    browser_clock_compatible = false;
    browser_wait_until_ms = 0;
    snapshot_capture_free(snapshot_capture);
    snapshot_capture = NULL;
    free(saved_snapshot);
    saved_snapshot = NULL;
    saved_snapshot_size = 0u;
    if (machine_ready) {
        s5l8900_free(&machine);
        machine_ready = false;
    }
}

static unsigned contact_mask(const touch_state_t *state) {
    unsigned mask = 0u;
    for (unsigned i = 0u; i < state->count; i++)
        mask |= 1u << state->contacts[i].id;
    return mask;
}

static bool release_button_now(unsigned which) {
    if (s5l8900_set_button(&machine, which, false)) {
        button_active[which] = false;
        button_release_pending[which] = false;
        button_force_release[which] = false;
        return true;
    }
    /* GPIO may still be servicing the down edge. Keep retrying rather than
     * dropping the only up event when focus moves to another browser tab. */
    button_release_pending[which] = true;
    return false;
}

static void flush_pending_inputs(void) {
    while (touch_queue_count) {
        const touch_state_t *next = &touch_queue[touch_queue_head];
        unsigned old_mask = contact_mask(&applied_touch);
        unsigned new_mask = contact_mask(next);
        for (unsigned id = 1u; id <= MTZ2_CONTACT_MAX; id++) {
            if ((old_mask & ~new_mask & (1u << id)) &&
                machine.timer.ticks - contact_down_ticks[id] <
                    EMU_TOUCH_HOLD_TICKS) goto buttons;
            /* A delayed worker turn can consume the hold deadline before
             * Z1's MAKE report is read. Keep a normal release queued until
             * the controller has generated its first held report. */
            if (machine.mtz1.firmware && (old_mask & ~new_mask & (1u << id)))
                for (unsigned i = 0u; i < machine.mtz1.last_n; i++)
                    if (machine.mtz1.last_contacts[i].id == id &&
                        machine.mtz1.last_contacts[i].phase == MTZ2_PHASE_MAKE_TOUCH)
                        goto buttons;
        }
        if (!s5l8900_set_touch(&machine, next->contacts, next->count))
            break;
        for (unsigned id = 1u; id <= MTZ2_CONTACT_MAX; id++)
            if (new_mask & ~old_mask & (1u << id))
                contact_down_ticks[id] = machine.timer.ticks;
        applied_touch = *next;
        touch_queue_head = (touch_queue_head + 1u) % EMU_TOUCH_QUEUE_CAPACITY;
        touch_queue_count--;
    }
buttons:
    for (unsigned which = 0u; which < S5L_BUTTON_COUNT; which++) {
        if (button_release_pending[which] &&
            (button_force_release[which] ||
             machine.timer.ticks - button_down_ticks[which] >=
                 EMU_BUTTON_HOLD_TICKS))
            (void)release_button_now(which);
    }
}

static int enqueue_touch(const touch_state_t *next) {
    touch_state_t state = *next;
    state.motion = contact_mask(next) == contact_mask(&requested_touch);
    if (touch_queue_count && state.motion) {
        unsigned tail = (touch_queue_head + touch_queue_count - 1u) %
                        EMU_TOUCH_QUEUE_CAPACITY;
        if (touch_queue[tail].motion &&
            contact_mask(&touch_queue[tail]) == contact_mask(next)) {
            touch_queue[tail] = state;
            requested_touch = state;
            return 0;
        }
    }
    if (touch_queue_count == EMU_TOUCH_QUEUE_CAPACITY) {
        set_error("touch input queue is full; release inputs before retrying");
        return -3;
    }
    unsigned tail = (touch_queue_head + touch_queue_count) % EMU_TOUCH_QUEUE_CAPACITY;
    touch_queue[tail] = state;
    touch_queue_count++;
    requested_touch = state;
    flush_pending_inputs();
    return 0;
}

EMSCRIPTEN_KEEPALIVE
const char *emu_error(void) {
    return error_text;
}

EMSCRIPTEN_KEEPALIVE
int emu_init(const uint8_t *kernel, size_t kernel_size,
             const uint8_t *devicetree, size_t devicetree_size,
             const uint8_t *rootfs, size_t rootfs_size, unsigned ram_mb) {
    const s5l_board_t *board;
    s5l_os1_request_t request;
    s5l_os1_result_t result;

    set_error("");
    release_machine();
    serial_read_pos = serial_write_pos = 0u;
    last_status = ARM_OK;
    reset_host_inputs();
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
#ifdef S5LBOX_WASM_JIT
    web_jit_bind(&machine);
#endif
    if (!s5l8900_set_direct_ram_writes(&machine, true)) {
        set_error("direct RAM write cache initialization failed");
        release_machine();
        return -6;
    }
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
    /* The current core boots the genuine detached USB stack. Suppressing it
     * makes lockdownd exit and adds connection retries to Home navigation. */
    request.unmatch = NULL;
    request.unmatch_count = 0u;
    if (!s5l_os1_boot(&machine, &request, &result)) {
        set_error(result.detail[0] ? result.detail : "OS1 boot setup failed");
        release_machine();
        return -4;
    }
    browser_clock_compatible = browser_guest_clock_compatible();
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
    browser_clock_compatible = browser_guest_clock_compatible();
    browser_wait_until_ms = 0;
    reset_host_inputs();
    last_status = ARM_OK;
    /* v39 snapshots omit Z1's uptime accumulator. Re-anchor its reports to
     * restored device ticks without changing the on-disk schema. */
    machine.mtz1.uptime_ms = (uint32_t)(machine.timer.ticks / MTZ1_TICKS_PER_MS);
    machine.mtz1.uptime_tick_acc = (uint32_t)(machine.timer.ticks % MTZ1_TICKS_PER_MS);
    (void)emu_release_inputs();
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
    browser_wait_until_ms = 0;
    while (total < max_steps) {
        unsigned slice = max_steps - total;
        unsigned ran;
        if (slice > 200000u)
            slice = 200000u;
        flush_pending_inputs();
        ran = s5l8900_run(&machine, slice, &last_status);
        total += ran;
        flush_pending_inputs();
        if (!ran || browser_wait_until_ms > 0)
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

/* Six uint32_t words per active contact: id,x,y,pressure,major,minor.
 * Success means accepted into the bounded host queue; emu_run drains it. */
EMSCRIPTEN_KEEPALIVE
int emu_touch_contacts(const uint32_t *records, unsigned count) {
    touch_state_t next = {0};
    unsigned ids = 0u;
    if (!machine_ready) {
        set_error("machine is not initialized");
        return -1;
    }
    if (count > MTZ2_CONTACT_MAX || (count && !records)) {
        set_error("touch contacts require zero to five records");
        return -2;
    }
    next.count = count;
    for (unsigned i = 0u; i < count; i++) {
        const uint32_t *r = records + 6u * i;
        if (!r[0] || r[0] > MTZ2_CONTACT_MAX || (ids & (1u << r[0])) ||
            r[1] >= S5L_MT_PANEL_W || r[2] >= S5L_MT_PANEL_H ||
            !r[3] || r[3] > 255u || !r[4] || r[4] > 255u ||
            !r[5] || r[5] > 255u) {
            set_error("invalid contact: unique id 1..5, panel coordinates, pressure/axes 1..255 required");
            return -2;
        }
        ids |= 1u << r[0];
        next.contacts[i] = (s5l_mt_contact_t){(uint8_t)r[0], MTZ2_PHASE_TOUCHING,
            (uint16_t)r[1], (uint16_t)r[2], (uint8_t)r[3], (uint8_t)r[4], (uint8_t)r[5]};
    }
    return enqueue_touch(&next);
}

EMSCRIPTEN_KEEPALIVE
int emu_touch(int down, unsigned x, unsigned y) {
    if (down == 0) return emu_touch_contacts(NULL, 0u);
    if (down != 1 && down != 2) {
        set_error("touch action must be release, down, or move");
        return -2;
    }
    const uint32_t record[6] = {1u, x, y, 255u, 8u, 8u};
    return emu_touch_contacts(record, 1u);
}

EMSCRIPTEN_KEEPALIVE
int emu_release_inputs(void) {
    if (!machine_ready) {
        set_error("machine is not initialized");
        return -1;
    }
    /* Discard host gestures that have not reached the device. The controller
     * retains any pending guest report and emits its break after consumption. */
    touch_queue_head = touch_queue_count = 0u;
    memset(&requested_touch, 0, sizeof requested_touch);
    memset(&applied_touch, 0, sizeof applied_touch);
    if (machine.mtz1.firmware && machine.mtz1.held_n &&
        !s5l8900_set_touch(&machine, NULL, 0u)) {
        set_error("unable to schedule restored touch release");
        return -2;
    }
    for (unsigned which = 0u; which < S5L_BUTTON_COUNT; which++) {
        if (which == S5L_BUTTON_RINGERAB) continue;
        button_active[which] = s5l_buttons_held(&machine.buttons, which);
        button_release_pending[which] = button_active[which];
        button_force_release[which] = button_active[which];
        if (button_active[which]) (void)release_button_now(which);
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_button(unsigned which, int pressed) {
    if (!machine_ready) {
        set_error("machine is not initialized");
        return -1;
    }
    if (which >= S5L_BUTTON_COUNT) {
        set_error("unknown device button");
        return -2;
    }
    if (which == S5L_BUTTON_RINGERAB) {
        if (!s5l8900_set_button(&machine, which, pressed != 0)) {
            set_error("ringer switch is busy");
            return -3;
        }
        button_active[which] = pressed != 0;
        return 0;
    }
    if (pressed) {
        if (button_release_pending[which]) {
            release_button_now(which);
            if (button_release_pending[which]) {
                set_error("button release is still pending");
                return -3;
            }
        }
        if (!s5l8900_set_button(&machine, which, true)) {
            set_error("button interrupt is not ready");
            return -3;
        }
        if (!button_active[which])
            button_down_ticks[which] = machine.timer.ticks;
        button_active[which] = true;
        button_release_pending[which] = false;
        button_force_release[which] = false;
        return 0;
    }
    if (!button_active[which])
        return s5l8900_set_button(&machine, which, false) ? 0 : -3;
    if (machine.timer.ticks - button_down_ticks[which] <
        EMU_BUTTON_HOLD_TICKS) {
        button_release_pending[which] = true;
        return 0;
    }
    (void)release_button_now(which);
    /* A deferred release has been accepted and is retried by emu_run. */
    return 0;
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

/* Shifted-out I2S PCM: interleaved signed 16-bit stereo frames at
 * emu_audio_rate(). Returns the number of whole frames copied. */
EMSCRIPTEN_KEEPALIVE
int emu_audio_read(int16_t *out, unsigned max_frames) {
    if (!out || !machine_ready)
        return 0;
    return (int)s5l8900_audio_read(&machine, out, max_frames);
}

EMSCRIPTEN_KEEPALIVE
int emu_audio_rate(void) {
    return machine_ready ? (int)s5l8900_audio_rate(&machine) : 0;
}

/* Snapshot allocation belongs to this API, and remains valid until the next
 * save/free/init. Copy it out of WASM before emu_free_snapshot(). */
EMSCRIPTEN_KEEPALIVE
uint8_t *emu_save_snapshot(void) {
    snapshot_capture_free(snapshot_capture);
    snapshot_capture = NULL;
    free(saved_snapshot);
    saved_snapshot = NULL;
    saved_snapshot_size = 0u;
    if (!machine_ready) {
        set_error("machine is not initialized");
        return NULL;
    }
    snapshot_status_t status = snapshot_save_mem(&machine, &saved_snapshot,
                                                 &saved_snapshot_size);
    if (status != SNAP_OK) {
        free(saved_snapshot);
        saved_snapshot = NULL;
        saved_snapshot_size = 0u;
        set_error(snapshot_strerror(status));
    }
    return saved_snapshot;
}

EMSCRIPTEN_KEEPALIVE
size_t emu_snapshot_size(void) {
    return saved_snapshot_size;
}

EMSCRIPTEN_KEEPALIVE
void emu_free_snapshot(void) {
    snapshot_capture_free(snapshot_capture);
    snapshot_capture = NULL;
    free(saved_snapshot);
    saved_snapshot = NULL;
    saved_snapshot_size = 0u;
}

EMSCRIPTEN_KEEPALIVE
int emu_snapshot_begin(void) {
    emu_free_snapshot();
    if (!machine_ready) { set_error("machine is not initialized"); return -1; }
    snapshot_status_t status = snapshot_capture_begin(&machine, &snapshot_capture);
    if (status != SNAP_OK) { set_error(snapshot_strerror(status)); return -2; }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_snapshot_advance(unsigned max_bytes) {
    if (!snapshot_capture) { set_error("no capture is in progress"); return -1; }
    return snapshot_capture_advance(snapshot_capture, max_bytes) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
const uint8_t *emu_snapshot_data(void) {
    return snapshot_capture_data(snapshot_capture, &saved_snapshot_size);
}

EMSCRIPTEN_KEEPALIVE
double emu_ticks(void) {
    return machine_ready ? (double)machine.timer.ticks : 0.0;
}

/* Host-side pointer-up can precede its guest break by several run slices. */
EMSCRIPTEN_KEEPALIVE
int emu_inputs_idle(void) {
    if (!machine_ready || touch_queue_count || machine.mtz1.held_n ||
        machine.mtz1.lift_pending || machine.mtz1.pending_len) return 0;
    for (unsigned which = 0u; which < S5L_BUTTON_COUNT; which++) {
        if (which == S5L_BUTTON_RINGERAB) continue;
        if (button_release_pending[which] ||
            s5l_buttons_held(&machine.buttons, which)) return 0;
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int emu_button_state(unsigned which) {
    if (!machine_ready || which >= S5L_BUTTON_COUNT) return -1;
    return s5l_buttons_held(&machine.buttons, which) ? 1 : 0;
}
