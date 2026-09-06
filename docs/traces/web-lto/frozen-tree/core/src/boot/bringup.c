/*
 * S5LBox — bringing up the real iPhone OS 3.1.3 kernel. See bringup.h.
 *
 * WHERE THESE NUMBERS COME FROM. Every address, alignment and property value
 * below is transcribed from tools/bootkernel.c, which is the implementation
 * ~90 recorded desktop runs were made with. None of it is re-derived here, and
 * where bootkernel.c carries the evidence for a value in a comment, the
 * pointer to that comment is carried here instead of the argument, so there
 * is exactly one place to correct if the evidence changes.
 *
 * The cross-check is core/tests/test_bringup.c: it runs this against the real
 * firmware/kernel.macho and firmware/devicetree.bin and requires the layout it
 * produces to be the one run89-base printed, address for address.
 *
 * ORDER MATTERS, and not for a stylistic reason:
 *
 *   1. parse the kernel                 — the layout starts at its vm_high
 *   2. plan every range and prove them disjoint, BEFORE any write
 *   3. load the segments
 *   4. run the caller's kernel gate     — it wants both file and loaded image
 *   5. copy the device tree in, patch it THERE
 *   6. build and load boot_args
 *   7. seed the display controller
 *   8. install the memory-disk bridges
 *   9. set PC and r0 — last, so a failure anywhere above leaves a machine
 *      that provably cannot be started by accident
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "bringup.h"
#include "dt_patch.h"

#include "arm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * S5L8900 clock rates, transcribed from tools/bootkernel.c:2350-2381 with its
 * sourcing intact.
 *
 * RESEARCHED: TB 6 MHz and FIX 24 MHz are openiBoot's plat-s5l8900/clock.c
 * (FREQUENCY_BASE 12 MHz, /2 and *2), corroborated by mach_absolute_time()
 * measurements on the original iPhone and the 3G. CPU 412 MHz is the
 * documented iPhone 3G core clock.
 *
 * PROVISIONAL: BUS, MEM and PRF are the self-consistent ratios of the S5L8900
 * clock tree (AHB = core/4, peripheral = AHB/2). Nothing the kernel does with
 * them is sensitive to the exact value — only to their being non-zero and to
 * (2*CPU)/BUS being a sane small integer. Replace if an iPhone1,2 IORegistry
 * dump ever surfaces.
 *
 * They are NOT decoration. The shipped tree carries zero in every frequency
 * property; pe_identify_machine() copies those zeros into
 * gPEClockFrequencyInfo and then divides by them, and rtclock.c:132 panics
 * when timebase_num < timebase_den.
 * ------------------------------------------------------------------------- */
#define S5L_BRINGUP_CPU_HZ  412000000u
#define S5L_BRINGUP_BUS_HZ  103000000u
#define S5L_BRINGUP_MEM_HZ  103000000u
#define S5L_BRINGUP_PRF_HZ   51500000u
#define S5L_BRINGUP_FIX_HZ   24000000u
#define S5L_BRINGUP_TB_HZ     6000000u

/*
 * iBoot reads the three Syrah panel-identification bytes as a big-endian
 * 24-bit value (openiBoot, plat-s5l8900/lcd.c:syrah_init); its N82 hardware
 * log records a5:c2:2b. AppleMerlotLCD rejects ID zero at 0xc0651f60, so the
 * template's zero is a placeholder rather than a value.
 */
#define S5L_BRINGUP_LCD_PANEL_ID  UINT32_C(0x00a5c22b)
#define S5L_BRINGUP_LCD_NODE      "arm-io/spi0/lcd0"
#define S5L_BRINGUP_LCD_COMPAT    "lcd,merlot"

/* The Darwin dev_t the raw bridge answers for, and the user/kernel VA split
 * it refuses to translate above. Both from bootkernel.c:59 and :25246. */
#define S5L_BRINGUP_MD_RAW_DEVICE       UINT32_C(0x09000000)
#define S5L_BRINGUP_USER_ADDRESS_LIMIT  UINT32_C(0xc0000000)

/*
 * The four Thumb SVC halfwords tools/ios3_kernel_patch.c writes over the two
 * memory-disk call sites and the raw-mdev prologue. They are configuration for
 * the bridges, not knowledge about the firmware: the gate owns which bytes go
 * where, and refuses a kernel that does not already contain what it expects.
 */
#define S5L_BRINGUP_SVC_MD_READ     UINT32_C(0xdfe1)
#define S5L_BRINGUP_SVC_MD_WRITE    UINT32_C(0xdfe2)
#define S5L_BRINGUP_SVC_RAW_ENTRY   UINT32_C(0xdfe3)
#define S5L_BRINGUP_SVC_RAW_DONE    UINT32_C(0xdfe4)

/* boot_args is 0x38 bytes of header plus a 256-byte command-line field. */
#define S5L_BRINGUP_BOOT_ARGS_SIZE  0x138u

/* Property-record shape of Apple's flat tree: char name[32], u32 length. */
#define DT_NAME_LEN   32u
#define DT_PROP_HDR   36u
#define DT_NO_NODE    ((size_t)-1)

/* ---------------------------------------------------------------------------
 * Overflow-safe arithmetic and range bookkeeping.
 *
 * Deliberately a transcription of bootkernel.c:245-261 and :1454-1500 rather
 * than an independent implementation: the property being reproduced is which
 * additions are checked, and a "cleaner" version that checks a different set
 * is a different program.
 * ------------------------------------------------------------------------- */
typedef struct {
    const char *name;
    uint64_t    begin;
    uint64_t    end;
    bool        active;
} bringup_range_t;

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out || b > UINT64_MAX - a) return false;
    *out = a + b;
    return true;
}

static bool align_u64(uint64_t value, uint64_t alignment, uint64_t *out) {
    if (!alignment || (alignment & (alignment - 1)) ||
        value > UINT64_MAX - (alignment - 1)) return false;
    *out = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

static bool range_make(bringup_range_t *range, const char *name,
                       uint64_t begin, uint64_t length, bool active) {
    if (!range || !add_u64(begin, length, &range->end)) return false;
    range->name   = name;
    range->begin  = begin;
    range->active = active;
    return true;
}

static bool ranges_overlap(const bringup_range_t *a, const bringup_range_t *b) {
    if (!a->active || !b->active || a->begin == a->end || b->begin == b->end)
        return false;
    return a->begin < b->end && b->begin < a->end;
}

/* ---------------------------------------------------------------------------
 * Result bookkeeping, in the shape tools/rootfs_work.c uses: one status, the
 * stage it was reached at, and a formatted line naming the specific thing.
 * ------------------------------------------------------------------------- */
static void result_reset(s5l_bringup_result_t *result) {
    memset(result, 0, sizeof *result);
    result->status       = S5L_BRINGUP_OK;
    result->stage        = S5L_BRINGUP_STAGE_NONE;
    result->macho_status = MACHO_OK;
    result->cmdline[0]   = '\0';
    result->detail[0]    = '\0';
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
static s5l_bringup_status_t result_fail(s5l_bringup_result_t *result,
                                        s5l_bringup_status_t status,
                                        s5l_bringup_stage_t stage,
                                        const char *format, ...) {
    va_list arguments;

    result->status = status;
    result->stage  = stage;
    va_start(arguments, format);
    (void)vsnprintf(result->detail, sizeof result->detail, format, arguments);
    va_end(arguments);
    result->detail[sizeof result->detail - 1u] = '\0';
    return status;
}

const char *s5l_bringup_status_name(s5l_bringup_status_t status) {
    switch (status) {
    case S5L_BRINGUP_OK:                        return "ok";
    case S5L_BRINGUP_INVALID_ARGUMENT:          return "invalid-argument";
    case S5L_BRINGUP_KERNEL_MISSING:            return "kernel-missing";
    case S5L_BRINGUP_KERNEL_MALFORMED:          return "kernel-malformed";
    case S5L_BRINGUP_KERNEL_NOT_EXECUTABLE:     return "kernel-not-executable";
    case S5L_BRINGUP_KERNEL_NO_ENTRY:           return "kernel-no-entry";
    case S5L_BRINGUP_KERNEL_BELOW_VIRT_BASE:    return "kernel-below-virt-base";
    case S5L_BRINGUP_KERNEL_SPAN_UNSAFE:        return "kernel-span-unsafe";
    case S5L_BRINGUP_ENTRY_OUTSIDE_KERNEL:      return "entry-outside-kernel";
    case S5L_BRINGUP_DEVICETREE_MISSING:        return "devicetree-missing";
    case S5L_BRINGUP_DEVICETREE_MALFORMED:      return "devicetree-malformed";
    case S5L_BRINGUP_DEVICETREE_TOO_LARGE:      return "devicetree-too-large";
    case S5L_BRINGUP_DEVICETREE_PATCH_FAILED:   return "devicetree-patch-failed";
    case S5L_BRINGUP_LAYOUT_OVERFLOW:           return "layout-overflow";
    case S5L_BRINGUP_LAYOUT_NO_ROOM:            return "layout-no-room";
    case S5L_BRINGUP_LAYOUT_OVERLAP:            return "layout-overlap";
    case S5L_BRINGUP_CMDLINE_TOO_LONG:          return "cmdline-too-long";
    case S5L_BRINGUP_FRAMEBUFFER_REFUSED:       return "framebuffer-refused";
    case S5L_BRINGUP_ROOT_MEDIA_INVALID:        return "root-media-invalid";
    case S5L_BRINGUP_ROOT_GATE_MISSING:         return "root-gate-missing";
    case S5L_BRINGUP_ROOT_GATE_REFUSED:         return "root-gate-refused";
    case S5L_BRINGUP_ROOT_BRIDGE_REFUSED:       return "root-bridge-refused";
    case S5L_BRINGUP_ROOT_STORAGE_MISSING:      return "root-storage-missing";
    }
    return "unknown";
}

const char *s5l_bringup_stage_name(s5l_bringup_stage_t stage) {
    switch (stage) {
    case S5L_BRINGUP_STAGE_NONE:              return "none";
    case S5L_BRINGUP_STAGE_ARGUMENTS:         return "arguments";
    case S5L_BRINGUP_STAGE_KERNEL_PARSE:      return "kernel-parse";
    case S5L_BRINGUP_STAGE_LAYOUT:            return "layout";
    case S5L_BRINGUP_STAGE_KERNEL_LOAD:       return "kernel-load";
    case S5L_BRINGUP_STAGE_KERNEL_GATE:       return "kernel-gate";
    case S5L_BRINGUP_STAGE_DEVICETREE_LOAD:   return "devicetree-load";
    case S5L_BRINGUP_STAGE_DEVICETREE_PATCH:  return "devicetree-patch";
    case S5L_BRINGUP_STAGE_COMMAND_LINE:      return "command-line";
    case S5L_BRINGUP_STAGE_BOOT_ARGS:         return "boot-args";
    case S5L_BRINGUP_STAGE_FRAMEBUFFER:       return "framebuffer";
    case S5L_BRINGUP_STAGE_ROOT_BRIDGE:       return "root-bridge";
    case S5L_BRINGUP_STAGE_ENTRY:             return "entry";
    }
    return "unknown";
}

/* ---------------------------------------------------------------------------
 * In-place, same-length device-tree writing — iBoot's job.
 *
 * Apple's flat format has no relocation table: every offset is implicit in the
 * byte stream, so growing a property means rewriting everything after it.
 * Every writer here therefore refuses to change a length and fails closed if
 * the property is not the shape it expects.
 *
 * These are structurally the same walkers as tools/dt_inplace.h, which is what
 * the desktop harness uses and what core/tests/test_devicetree.c pins. They
 * are re-stated rather than included because dt_inplace.h reports through
 * printf() to a terminal nobody is watching on a phone, and because emucore
 * must not acquire a diagnostic-output dependency. test_bringup.c closes the
 * gap the honest way: it reads the tree this file produced back through
 * dt_inplace.h's readers, so the two implementations are checked against each
 * other on real firmware rather than trusted to agree.
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * The SVC multiplexer.
 *
 * One privileged-SVC callback has to reach two bridges. Order is not
 * arbitrary: the strategy bridge claims 0xdfe1/0xdfe2 and returns UNHANDLED
 * for anything else, so the raw bridge sees only what is left. A NULL context
 * returns ERROR rather than UNHANDLED, because an SVC at a patched site that
 * falls through to the guest's own handler is a silent wrong answer, and
 * ARM_SVC_ERROR halts the core instead.
 * ------------------------------------------------------------------------- */
static arm_svc_result_t bringup_svc_handler(void *context, arm_cpu_t *cpu,
                                            uint32_t pc, uint32_t encoding) {
    s5l_bringup_md_t *md = (s5l_bringup_md_t *)context;
    arm_svc_result_t result;

    if (md == NULL || !md->installed) return ARM_SVC_ERROR;
    result = md_bridge_handle_svc(&md->strategy, cpu, pc, encoding);
    if (result != ARM_SVC_UNHANDLED) return result;
    return md_raw_bridge_handle_svc(&md->raw, cpu, pc, encoding);
}

/* A boot-argument key is one complete whitespace-delimited token up to '='.
 * Matching tokens rather than substrings keeps `rd` distinct from `board`,
 * and lets an explicit `key=0` override an opt-in request instead of silently
 * appending a contradictory `key=1`. */
static bool cmdline_has_key(const char *line, const char *key) {
    size_t key_len;

    if (!line || !key || !*key) return false;
    key_len = strlen(key);
    while (*line) {
        const char *start;
        size_t length;

        while (*line == ' ' || *line == '\t') line++;
        if (!*line) break;
        start = line;
        while (*line && *line != ' ' && *line != '\t') line++;
        length = (size_t)(line - start);
        if (length > key_len && start[key_len] == '=' &&
            memcmp(start, key, key_len) == 0)
            return true;
    }
    return false;
}

static bool cmdline_append(char *line, size_t capacity, const char *token) {
    size_t used;
    int written;

    if (!line || !capacity || !token || !*token) return false;
    used = strlen(line);
    if (used >= capacity) return false;
    written = snprintf(line + used, capacity - used, "%s%s",
                       used ? " " : "", token);
    return written >= 0 && (size_t)written < capacity - used;
}

/* ------------------------------------------------------------------------- */

s5l_bringup_status_t s5l_bringup(s5l8900_t *machine,
                                 const s5l_bringup_request_t *request,
                                 s5l_bringup_md_t *md,
                                 s5l_bringup_result_t *result) {
    if (!result) return S5L_BRINGUP_INVALID_ARGUMENT;
    result_reset(result);

    if (!machine || !request || !machine->ram)
        return result_fail(result, S5L_BRINGUP_INVALID_ARGUMENT,
                           S5L_BRINGUP_STAGE_ARGUMENTS,
                           "no machine or no request");

    const uint32_t virt_base = S5L_BRINGUP_VIRT_BASE;
    const uint32_t phys_base = machine->ram_base;
    const uint32_t ram_size  = machine->ram_size;

    result->virt_base = virt_base;
    result->phys_base = phys_base;
    result->ram_size  = ram_size;

    /* The kernel's segments carry 0xc0000000-based virtual addresses and the
     * MMU is off at entry, so this mapping is the whole boot: it decides where
     * every byte of the image physically lands. A machine built anywhere else
     * cannot run this kernel, so say so here rather than 8 MB of writes later. */
    if (phys_base != S5L_BRINGUP_PHYS_BASE || ram_size != S5L_BRINGUP_RAM_SIZE)
        return result_fail(result, S5L_BRINGUP_INVALID_ARGUMENT,
                           S5L_BRINGUP_STAGE_ARGUMENTS,
                           "machine is 0x%08x+%u MB; this kernel requires "
                           "0x%08x+%u MB",
                           phys_base, ram_size >> 20,
                           S5L_BRINGUP_PHYS_BASE,
                           S5L_BRINGUP_RAM_SIZE >> 20);

    if (!request->kernel || request->kernel_size == 0u)
        return result_fail(result, S5L_BRINGUP_KERNEL_MISSING,
                           S5L_BRINGUP_STAGE_ARGUMENTS,
                           "no kernel image supplied");
    if (!request->devicetree || request->devicetree_size == 0u)
        return result_fail(result, S5L_BRINGUP_DEVICETREE_MISSING,
                           S5L_BRINGUP_STAGE_ARGUMENTS,
                           "no device tree supplied");
    if (request->devicetree_size > UINT32_MAX)
        return result_fail(result, S5L_BRINGUP_DEVICETREE_TOO_LARGE,
                           S5L_BRINGUP_STAGE_ARGUMENTS,
                           "device tree is %llu bytes; boot_args carries a "
                           "32-bit length",
                           (unsigned long long)request->devicetree_size);

    const bool want_fb   = !request->no_framebuffer;
    const bool want_root = request->root_media != NULL;

    if (want_root) {
        if (!md)
            return result_fail(result, S5L_BRINGUP_ROOT_STORAGE_MISSING,
                               S5L_BRINGUP_STAGE_ARGUMENTS,
                               "a root filesystem needs bridge storage; "
                               "none was supplied");
        if (!request->kernel_gate)
            /* Without the gate the two Thumb call sites are never replaced,
             * so no SVC ever reaches the bridge and the guest reads whatever
             * DRAM happens to be at 0xe0000000 — nothing. Refuse rather than
             * boot something that will fail far away from the cause. */
            return result_fail(result, S5L_BRINGUP_ROOT_GATE_MISSING,
                               S5L_BRINGUP_STAGE_ARGUMENTS,
                               "a root filesystem needs a kernel gate to "
                               "install the memory-disk SVC sites");
        /* A zero site would make the bridge watch address zero, which the
         * guest never executes, so every transfer would fall through to a
         * kernel whose call site has been replaced by a trap. */
        if (!request->md_read_site_pc || !request->md_write_site_pc ||
            !request->md_raw_site_pc || !request->uiomove_pc)
            return result_fail(result, S5L_BRINGUP_ROOT_GATE_MISSING,
                               S5L_BRINGUP_STAGE_ARGUMENTS,
                               "the memory-disk SVC site addresses are "
                               "incomplete (read 0x%08x write 0x%08x raw "
                               "0x%08x uiomove 0x%08x)",
                               request->md_read_site_pc,
                               request->md_write_site_pc,
                               request->md_raw_site_pc, request->uiomove_pc);
    }
    if (md) {
        memset(md, 0, sizeof *md);
        md->installed = false;
    }

    /* --- 1. parse the kernel ---------------------------------------------- */
    macho_t image;
    macho_status_t macho_status =
        macho_parse(request->kernel, request->kernel_size, &image);
    if (macho_status != MACHO_OK) {
        result->macho_status = macho_status;
        return result_fail(result, S5L_BRINGUP_KERNEL_MALFORMED,
                           S5L_BRINGUP_STAGE_KERNEL_PARSE,
                           "not a loadable Mach-O: %s",
                           macho_strerror(macho_status));
    }
    if (image.filetype != MH_EXECUTE || image.cputype != MH_CPU_TYPE_ARM)
        return result_fail(result, S5L_BRINGUP_KERNEL_NOT_EXECUTABLE,
                           S5L_BRINGUP_STAGE_KERNEL_PARSE,
                           "cputype %u filetype %u is not an ARM executable",
                           image.cputype, image.filetype);
    if (!image.has_entry)
        return result_fail(result, S5L_BRINGUP_KERNEL_NO_ENTRY,
                           S5L_BRINGUP_STAGE_KERNEL_PARSE,
                           "image carries no LC_UNIXTHREAD entry point");
    if (image.vm_low < virt_base || image.vm_high < image.vm_low)
        return result_fail(result, S5L_BRINGUP_KERNEL_BELOW_VIRT_BASE,
                           S5L_BRINGUP_STAGE_KERNEL_PARSE,
                           "virtual span [0x%08x,0x%08x) is not above the "
                           "virtual base 0x%08x",
                           image.vm_low, image.vm_high, virt_base);

    /* --- 2. plan the whole physical layout before writing anything -------- */
    bringup_range_t dram, kernel, tree, args, bounce, framebuffer;
    uint64_t kernel_begin, kernel_end;
    if (!range_make(&dram, "DRAM", phys_base, ram_size, true) ||
        !add_u64(phys_base, (uint64_t)image.vm_low - virt_base, &kernel_begin) ||
        !add_u64(phys_base, (uint64_t)image.vm_high - virt_base, &kernel_end) ||
        !range_make(&kernel, "kernel", kernel_begin,
                    kernel_end - kernel_begin, true))
        return result_fail(result, S5L_BRINGUP_LAYOUT_OVERFLOW,
                           S5L_BRINGUP_STAGE_LAYOUT,
                           "kernel span [0x%08x,0x%08x) does not map from "
                           "0x%08x",
                           image.vm_low, image.vm_high, virt_base);
    if (kernel.begin < dram.begin || kernel.end > dram.end)
        return result_fail(result, S5L_BRINGUP_LAYOUT_NO_ROOM,
                           S5L_BRINGUP_STAGE_LAYOUT,
                           "kernel needs [0x%llx,0x%llx); DRAM is "
                           "[0x%llx,0x%llx)",
                           (unsigned long long)kernel.begin,
                           (unsigned long long)kernel.end,
                           (unsigned long long)dram.begin,
                           (unsigned long long)dram.end);
    result->kernel_begin_pa = (uint32_t)kernel.begin;
    result->kernel_end_pa   = (uint32_t)kernel.end;

    /* Both the tree and the boot_args page start on a 4 KiB boundary; the
     * bounce reservation on a 16 KiB one, because it is the last static input
     * and topOfKernelData is derived from its end. */
    uint64_t tree_pa, args_pa, bounce_pa = 0;
    if (!align_u64(kernel.end, 0x1000u, &tree_pa) || tree_pa > UINT32_MAX ||
        !range_make(&tree, "device tree", tree_pa,
                    request->devicetree_size, true) ||
        !align_u64(tree.end, 0x1000u, &args_pa) || args_pa > UINT32_MAX ||
        !range_make(&args, "boot_args page", args_pa, 0x1000u, true) ||
        (want_root && (!align_u64(args.end, 0x4000u, &bounce_pa) ||
                       bounce_pa > UINT32_MAX)) ||
        !range_make(&bounce, "raw md bounce reserve", bounce_pa,
                    want_root ? S5L_BRINGUP_MD_RAW_RESERVE_SIZE : 0u,
                    want_root))
        return result_fail(result, S5L_BRINGUP_LAYOUT_OVERFLOW,
                           S5L_BRINGUP_STAGE_LAYOUT,
                           "kernel, device tree and boot_args do not fit in "
                           "32-bit physical space");

    /*
     * The framebuffer is boot-owned memory just like the tree and boot_args.
     * Leaving it near the top of DRAM while topOfKernelData stopped below it
     * advertised the scanout pages to XNU's free-page allocator, so it goes
     * immediately after the static reserve and its end is included in the
     * line. 16 KiB aligned for the reason topOfKernelData is.
     */
    const uint32_t fb_bytes = want_fb ? S5L_BRINGUP_VRAM_BYTES : 0u;
    uint64_t static_end = want_root ? bounce.end : args.end;
    uint64_t aligned_static_end;
    if (!align_u64(static_end, S5L_BRINGUP_TOKD_ALIGNMENT, &aligned_static_end) ||
        !range_make(&framebuffer, "framebuffer",
                    want_fb ? aligned_static_end : 0u, fb_bytes, want_fb))
        return result_fail(result, S5L_BRINGUP_LAYOUT_OVERFLOW,
                           S5L_BRINGUP_STAGE_LAYOUT,
                           "framebuffer reserve of %u bytes cannot be placed "
                           "after the static layout", fb_bytes);

    const bringup_range_t *plan[] = {
        &kernel, &tree, &args, &bounce, &framebuffer
    };
    for (size_t i = 0; i < sizeof plan / sizeof plan[0]; i++) {
        if (!plan[i]->active) continue;
        if (plan[i]->begin < dram.begin || plan[i]->end > dram.end)
            return result_fail(result, S5L_BRINGUP_LAYOUT_NO_ROOM,
                               S5L_BRINGUP_STAGE_LAYOUT,
                               "%s [0x%llx,0x%llx) is outside DRAM "
                               "[0x%llx,0x%llx)",
                               plan[i]->name,
                               (unsigned long long)plan[i]->begin,
                               (unsigned long long)plan[i]->end,
                               (unsigned long long)dram.begin,
                               (unsigned long long)dram.end);
        for (size_t j = 0; j < i; j++)
            if (ranges_overlap(plan[i], plan[j]))
                return result_fail(result, S5L_BRINGUP_LAYOUT_OVERLAP,
                                   S5L_BRINGUP_STAGE_LAYOUT,
                                   "%s [0x%llx,0x%llx) overlaps %s "
                                   "[0x%llx,0x%llx)",
                                   plan[i]->name,
                                   (unsigned long long)plan[i]->begin,
                                   (unsigned long long)plan[i]->end,
                                   plan[j]->name,
                                   (unsigned long long)plan[j]->begin,
                                   (unsigned long long)plan[j]->end);
    }

    /*
     * topOfKernelData: everything below it is memory the kernel treats as its
     * own pre-loaded static data and never hands to the VM, so the free page
     * pool the whole system runs out of is exactly [TOKD, end of DRAM).
     *
     * 16 KiB alignment is MEASURED, not stylistic: XNU derives its ARMv6 L1
     * translation-table base from this value and TTBR0[31:14] is that base. A
     * merely page-aligned value put the table at 0x093e5000, TTBR0 came out
     * 0x093e5018, and the hardware walked from 0x093e4000 — a prefetch abort
     * on the instruction immediately after the MMU was switched on.
     */
    uint64_t final_static_end = static_end;
    if (framebuffer.active && framebuffer.end > final_static_end)
        final_static_end = framebuffer.end;
    uint64_t top_of_kernel_data;
    if (!align_u64(final_static_end, S5L_BRINGUP_TOKD_ALIGNMENT,
                   &top_of_kernel_data) ||
        top_of_kernel_data > UINT32_MAX || top_of_kernel_data > dram.end ||
        dram.end - top_of_kernel_data < S5L_BRINGUP_BOOTSTRAP_HEADROOM)
        return result_fail(result, S5L_BRINGUP_LAYOUT_NO_ROOM,
                           S5L_BRINGUP_STAGE_LAYOUT,
                           "static boot reserve leaves less than 0x%llx bytes "
                           "of bootstrap headroom in DRAM",
                           (unsigned long long)S5L_BRINGUP_BOOTSTRAP_HEADROOM);

    result->devicetree_pa         = (uint32_t)tree.begin;
    result->devicetree_va         = virt_base + (uint32_t)(tree.begin - phys_base);
    result->devicetree_size       = (uint32_t)request->devicetree_size;
    result->boot_args_pa          = (uint32_t)args.begin;
    result->raw_bounce_pa         = want_root ? (uint32_t)bounce.begin : 0u;
    result->framebuffer_pa        = want_fb ? (uint32_t)framebuffer.begin : 0u;
    result->vram_bytes            = fb_bytes;
    result->framebuffer_enabled   = want_fb;
    result->top_of_kernel_data_pa = (uint32_t)top_of_kernel_data;
    result->top_of_kernel_data_va =
        virt_base + (uint32_t)(top_of_kernel_data - phys_base);
    result->free_pool_bytes = (uint32_t)(dram.end - top_of_kernel_data);

    /* Entry, checked against the kernel's own span before anything is loaded. */
    uint64_t entry_pa;
    if (image.entry < virt_base ||
        !add_u64(phys_base, (uint64_t)image.entry - virt_base, &entry_pa) ||
        entry_pa > UINT32_MAX || entry_pa < kernel.begin || entry_pa >= kernel.end)
        return result_fail(result, S5L_BRINGUP_ENTRY_OUTSIDE_KERNEL,
                           S5L_BRINGUP_STAGE_LAYOUT,
                           "entry 0x%08x is outside the kernel span "
                           "[0x%llx,0x%llx)",
                           image.entry, (unsigned long long)kernel.begin,
                           (unsigned long long)kernel.end);
    result->entry_va = image.entry;
    result->entry_pa = (uint32_t)entry_pa;

    /*
     * Root-media geometry, before the first write for the same reason as
     * everything else above. mdevadd takes base and size as PAGE NUMBERS
     * (addr >> 12), so a size that is not a page multiple is a silently
     * truncated disk.
     */
    uint64_t media_size = 0;
    if (want_root) {
        media_size = request->root_media->size;
        uint64_t token_end;
        if (media_size == 0u || media_size > S5L_BRINGUP_MD_MAX_SIZE ||
            media_size > UINT32_MAX || (media_size & 0xfffu) != 0u ||
            !request->root_media->read_at || !request->root_media->write_at ||
            !add_u64(S5L_BRINGUP_MD_TOKEN_BASE, media_size, &token_end))
            return result_fail(result, S5L_BRINGUP_ROOT_MEDIA_INVALID,
                               S5L_BRINGUP_STAGE_ARGUMENTS,
                               "root medium of %llu bytes is empty, unaligned, "
                               "or exceeds the supported token window at 0x%08x",
                               (unsigned long long)media_size,
                               (unsigned)S5L_BRINGUP_MD_TOKEN_BASE);
        result->root_media_size = media_size;
        result->root_dt_address = (uint32_t)S5L_BRINGUP_MD_TOKEN_BASE;
        result->root_dt_size    = (uint32_t)media_size;
    }

    /* --- 3. load the kernel's segments ------------------------------------ */
    /*
     * filesize, not vmsize: the tail from filesize to vmsize is BSS and is
     * already zero because s5l8900_init() hands out zeroed DRAM. A segment
     * below the virtual base is skipped rather than refused, matching
     * bootkernel.c — vm_low being above the base was already required, so this
     * only fires on an image whose segment list disagrees with its own span.
     */
    for (unsigned i = 0; i < image.segment_count; i++) {
        const macho_segment_t *segment = &image.segments[i];
        if (!segment->filesize) continue;
        if (segment->vmaddr < virt_base) continue;
        uint64_t segment_pa, segment_end;
        if (!add_u64(phys_base, (uint64_t)segment->vmaddr - virt_base,
                     &segment_pa) ||
            !add_u64(segment_pa, segment->filesize, &segment_end) ||
            segment_pa > UINT32_MAX || segment_pa < kernel.begin ||
            segment_end > kernel.end || segment_end > dram.end)
            return result_fail(result, S5L_BRINGUP_KERNEL_SPAN_UNSAFE,
                               S5L_BRINGUP_STAGE_KERNEL_LOAD,
                               "segment %s has an unsafe physical span",
                               segment->name);
        s5l8900_load(machine, (uint32_t)segment_pa,
                     request->kernel + segment->fileoff, segment->filesize);
        result->segments_loaded++;
    }

    /* --- 4. the caller's kernel gate -------------------------------------- */
    /*
     * Runs with both the file and the loaded image visible, which is what lets
     * tools/ios3_kernel_patch.c prove that guest RAM holds the exact bytes it
     * has patch offsets for before it writes any of them.
     */
    if (want_root) {
        char gate_detail[S5L_BRINGUP_DETAIL_CAPACITY];
        gate_detail[0] = '\0';
        if (!request->kernel_gate(request->kernel_gate_context,
                                  request->kernel, request->kernel_size,
                                  machine->ram, machine->ram_size,
                                  machine->ram_base, virt_base,
                                  gate_detail, sizeof gate_detail)) {
            gate_detail[sizeof gate_detail - 1u] = '\0';
            return result_fail(result, S5L_BRINGUP_ROOT_GATE_REFUSED,
                               S5L_BRINGUP_STAGE_KERNEL_GATE,
                               "%s", gate_detail[0] ? gate_detail
                                                    : "kernel not accepted");
        }
    }

    /* --- 5. the device tree, copied in and patched in place --------------- */
    /*
     * Copied into guest DRAM first and patched THERE, so the caller's buffer
     * is never modified and the patched bytes are exactly the bytes the kernel
     * will read. `tree_ram` is inside the machine's own allocation; the plan
     * above proved the whole span is in DRAM.
     */
    s5l8900_load(machine, (uint32_t)tree.begin, request->devicetree,
                 request->devicetree_size);
    uint8_t *tree_ram = machine->ram + (size_t)(tree.begin - dram.begin);
    size_t   tree_len = request->devicetree_size;

    {
        uint32_t n_props, n_children;
        if (!s5l_dt_header(tree_ram, tree_len, 0, &n_props, &n_children) ||
            s5l_dt_subtree_end(tree_ram, tree_len, 0, 0) == 0)
            return result_fail(result, S5L_BRINGUP_DEVICETREE_MALFORMED,
                               S5L_BRINGUP_STAGE_DEVICETREE_LOAD,
                               "device tree is not a complete Apple flat tree");
    }

    /* The clock table. Every one of these is zero in the shipped template. */
    static const struct { const char *path, *prop; uint32_t value; } clocks[] = {
        { "",          "clock-frequency",      S5L_BRINGUP_BUS_HZ },
        { "cpus/cpu0", "timebase-frequency",   S5L_BRINGUP_TB_HZ  },
        { "cpus/cpu0", "clock-frequency",      S5L_BRINGUP_CPU_HZ },
        { "cpus/cpu0", "bus-frequency",        S5L_BRINGUP_BUS_HZ },
        { "cpus/cpu0", "memory-frequency",     S5L_BRINGUP_MEM_HZ },
        { "cpus/cpu0", "peripheral-frequency", S5L_BRINGUP_PRF_HZ },
        { "cpus/cpu0", "fixed-frequency",      S5L_BRINGUP_FIX_HZ },
    };
    for (size_t i = 0; i < sizeof clocks / sizeof clocks[0]; i++) {
        if (!s5l_dt_set_u32(tree_ram, tree_len, clocks[i].path, clocks[i].prop,
                        clocks[i].value))
            /* Fail closed. A missing frequency is a divide by zero inside
             * pe_identify_machine(), not a cosmetic omission. */
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "/%s:%s is absent or not 4 bytes",
                               *clocks[i].path ? clocks[i].path : "",
                               clocks[i].prop);
        result->devicetree_patches++;
    }

    if (request->guest_codesign_disabled) {
        if (!s5l_dt_set_u32(tree_ram, tree_len, "chosen", "debug-enabled", 1u))
            return result_fail(result,
                               S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "cannot set /chosen:debug-enabled; refusing "
                               "a half-applied guest code-signing policy");
        result->devicetree_patches++;
    }

    if (want_fb && !request->no_lcd_panel_id) {
        if (!s5l_dt_compatible_exact(tree_ram, tree_len, S5L_BRINGUP_LCD_NODE,
                                 S5L_BRINGUP_LCD_COMPAT))
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "/%s is not exactly \"%s\"; refusing a "
                               "panel-specific patch on the wrong node",
                               S5L_BRINGUP_LCD_NODE, S5L_BRINGUP_LCD_COMPAT);
        if (!s5l_dt_set_u32(tree_ram, tree_len, S5L_BRINGUP_LCD_NODE,
                        "lcd-panel-id", S5L_BRINGUP_LCD_PANEL_ID))
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "cannot patch the N82 lcd-panel-id; refusing a "
                               "half-configured display");
        result->devicetree_patches++;
    }

    /* /memory reg is {0,0} in the template, which advertises a zero-sized
     * bank of DRAM to a kernel that is about to allocate out of it. */
    if (!request->no_memory_node) {
        if (!s5l_dt_set_reg(tree_ram, tree_len, "memory", "reg",
                        phys_base, ram_size))
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "cannot publish DRAM geometry in /memory:reg");
        result->devicetree_patches++;
    }

    /*
     * /vram reg is the property that decides whether userspace may WRITE to
     * the screen. With it left at {0,0}, IOSurfaceDeviceMemoryRegion::init
     * fails, AppleH1CLCD takes its fallback and builds the descriptor with
     * kIODirectionOut, and IOSurfaceRootUserClient turns exactly that into
     * kIOMapReadOnly — run57 measured the consequence as an L2 descriptor with
     * user-RO permissions over the framebuffer page and FSR 0x80f with WnR
     * set, i.e. SpringBoard's compositor faulting on its first store.
     *
     * The value is the whole pool, not the scanout buffer.
     */
    if (want_fb) {
        if (!s5l_dt_set_reg(tree_ram, tree_len, "vram", "reg",
                        (uint32_t)framebuffer.begin, S5L_BRINGUP_VRAM_BYTES))
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "cannot patch /vram:reg; refusing a "
                               "half-configured display");
        result->devicetree_patches++;
    }

    /*
     * RAMDisk is published as a VIRTUAL-looking address even though every
     * other memory-map entry is physical by convention, because nothing on its
     * path converts it: _IOFindBSDRoot passes parms[0]>>12 straight to
     * _mdevadd with no _ml_static_ptovirt in sight, _mdevadd stores the page
     * number verbatim, and mdevstrategy then bcopy()s from (mdBase << 12) as a
     * kernel virtual address. Here that address is the synthetic token the
     * bridge answers for, so the bcopy traps instead of reading DRAM.
     */
    if (want_root) {
        if (!s5l_dt_memmap_add(tree_ram, tree_len, "RAMDisk",
                           result->root_dt_address, result->root_dt_size))
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "no free MemoryMapReserved-* placeholder for "
                               "the RAMDisk entry");
        result->devicetree_patches++;
    }

    /*
     * Un-matching, last, so every patch above read the tree as Apple shipped
     * it. Doing it first would mean a node could be struck out and then
     * patched, and the patch would be the one that fails -- naming the wrong
     * cause for a configuration the caller chose deliberately.
     */
    for (unsigned i = 0; i < request->unmatch_count; i++) {
        const char *path = request->unmatch ? request->unmatch[i] : NULL;
        if (!path || !*path)
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "unmatch[%u] is empty", i);
        size_t node = s5l_dt_path(tree_ram, tree_len, path);
        if (node == DT_NO_NODE)
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "/%s cannot be un-matched: no such node", path);
        uint32_t vl = 0;
        uint8_t *p = s5l_dt_prop(tree_ram, tree_len, node, "compatible", &vl);
        if (!p || !vl)
            return result_fail(result, S5L_BRINGUP_DEVICETREE_PATCH_FAILED,
                               S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
                               "/%s cannot be un-matched: no compatible", path);
        /* Idempotent: striking an already-struck node is not an error, so a
         * caller may name the same device twice without having to track it. */
        p[0] = 'x';
        result->devicetree_unmatched++;
    }

    /* --- 6. the command line and boot_args -------------------------------- */
    const char *base_cmdline = request->cmdline ? request->cmdline
                                                : S5L_BRINGUP_DEFAULT_CMDLINE;
    /*
     * IOFindBSDRoot compares rdBootVar[0..1] against "md" and rdBootVar[3]
     * against NUL, so the root token has to be exactly "md<digit>". Appended
     * only when the caller has not named a root device itself.
     *
     * Scanned as whitespace-delimited TOKENS rather than with strstr: "rd=" is
     * a substring of "board=1", and appending a second root device because the
     * first one was not recognised is exactly the kind of quiet wrongness that
     * ends in the guest mounting something nobody chose.
     */
    int written = snprintf(result->cmdline, sizeof result->cmdline, "%s",
                           base_cmdline);
    if (written < 0 || (size_t)written >= sizeof result->cmdline) {
        result->cmdline[0] = '\0';
        return result_fail(result, S5L_BRINGUP_CMDLINE_TOO_LONG,
                           S5L_BRINGUP_STAGE_COMMAND_LINE,
                           "command line exceeds the %u-byte boot_args field",
                           (unsigned)(S5L_BRINGUP_CMDLINE_CAPACITY - 1u));
    }

    static const struct { const char *key, *token; } codesign[] = {
        { "cs_enforcement_disable", "cs_enforcement_disable=1" },
        { "amfi_get_out_of_my_way", "amfi_get_out_of_my_way=1" },
        { "amfi_allow_any_signature", "amfi_allow_any_signature=1" },
    };
    if (request->guest_codesign_disabled) {
        for (size_t i = 0; i < sizeof codesign / sizeof codesign[0]; i++) {
            if (cmdline_has_key(result->cmdline, codesign[i].key)) continue;
            if (!cmdline_append(result->cmdline, sizeof result->cmdline,
                                codesign[i].token))
                return result_fail(result, S5L_BRINGUP_CMDLINE_TOO_LONG,
                                   S5L_BRINGUP_STAGE_COMMAND_LINE,
                                   "cannot append %s within the %u-byte "
                                   "boot_args field", codesign[i].token,
                                   (unsigned)(S5L_BRINGUP_CMDLINE_CAPACITY -
                                              1u));
        }
    }
    if (want_root && !cmdline_has_key(result->cmdline, "rd") &&
        !cmdline_append(result->cmdline, sizeof result->cmdline,
                        S5L_BRINGUP_ROOT_TOKEN))
        return result_fail(result, S5L_BRINGUP_CMDLINE_TOO_LONG,
                           S5L_BRINGUP_STAGE_COMMAND_LINE,
                           "cannot append %s within the %u-byte boot_args "
                           "field", S5L_BRINGUP_ROOT_TOKEN,
                           (unsigned)(S5L_BRINGUP_CMDLINE_CAPACITY - 1u));
    written = (int)strlen(result->cmdline);

    /*
     * XNU derives where to put its page tables from these fields; zeros make
     * it compute nonsense addresses, which is exactly what happened before
     * this existed (TTBR0 came out 0x18 and the MMU walked unmapped memory).
     */
    uint8_t boot_args[S5L_BRINGUP_BOOT_ARGS_SIZE];
    memset(boot_args, 0, sizeof boot_args);
#define PUT16(o, v) do { boot_args[o]        = (uint8_t)(v);         \
                         boot_args[(o) + 1u] = (uint8_t)((v) >> 8);  \
                       } while (0)
#define PUT32(o, v) do { boot_args[o]        = (uint8_t)(v);         \
                         boot_args[(o) + 1u] = (uint8_t)((v) >> 8);  \
                         boot_args[(o) + 2u] = (uint8_t)((v) >> 16); \
                         boot_args[(o) + 3u] = (uint8_t)((v) >> 24); \
                       } while (0)
    PUT16(0x00u, request->boot_args_revision ? request->boot_args_revision : 1u);
    /* Version 6 is MEASURED: pe_identify_machine() checks boot_args+2 and this
     * kernel accepts nothing else. Revision is not checked; 1 works. */
    PUT16(0x02u, request->boot_args_version ? request->boot_args_version : 6u);
    PUT32(0x04u, virt_base);
    PUT32(0x08u, phys_base);
    PUT32(0x0cu, ram_size);
    /* PHYSICAL, not virtual: the kernel uses this directly as the base for its
     * page tables. The virtual form made TTBR0 come out 0xc07dc018. */
    PUT32(0x10u, result->top_of_kernel_data_pa);
    PUT32(0x14u, result->framebuffer_pa);            /* v_baseAddr */
    /* v_display selects the console MODE, not whether a display exists.
     * _PE_create_console branches on it: non-zero picks kPEGraphicsMode, and
     * _vcattach then returns immediately, so the kernel text console is never
     * acquired and nothing is drawn. Zero picks kPETextMode and the kernel
     * paints its boot log where we can read it. */
    PUT32(0x18u, want_fb ? request->v_display : 0u);
    PUT32(0x1cu, S5L_BRINGUP_FB_WIDTH * S5L_BRINGUP_FB_BPP);  /* v_rowBytes */
    PUT32(0x20u, S5L_BRINGUP_FB_WIDTH);                       /* v_width    */
    PUT32(0x24u, S5L_BRINGUP_FB_HEIGHT);                      /* v_height   */
    PUT32(0x28u, S5L_BRINGUP_FB_BPP * 8u);                    /* v_depth    */
    PUT32(0x2cu, 0u);                                         /* machineType */
    /* VIRTUAL here, unlike topOfKernelData above: IODeviceTreeAlloc consumes
     * this through the kernel's own mapping. */
    PUT32(0x30u, result->devicetree_va);
    PUT32(0x34u, result->devicetree_size);
    memcpy(boot_args + 0x38u, result->cmdline, (size_t)written);
#undef PUT16
#undef PUT32
    s5l8900_load(machine, (uint32_t)args.begin, boot_args, sizeof boot_args);

    /* --- 7. seed the display controller ----------------------------------- */
    /*
     * Boot_Video describes ONE 320x480 framebuffer at the base of the /vram
     * pool and the CLCD is seeded to scan out that one; the rest of the pool
     * exists so IOSurface has somewhere to put a second surface.
     */
    if (want_fb &&
        !s5l_clcd_seed_window0(&machine->clcd, (uint32_t)framebuffer.begin,
                               S5L_BRINGUP_FB_WIDTH, S5L_BRINGUP_FB_HEIGHT,
                               S5L_BRINGUP_FB_WIDTH * S5L_BRINGUP_FB_BPP,
                               CLCD_FMT_32BPP, CLCD_ORDER_BGRA))
        return result_fail(result, S5L_BRINGUP_FRAMEBUFFER_REFUSED,
                           S5L_BRINGUP_STAGE_FRAMEBUFFER,
                           "the display controller rejected a validated "
                           "framebuffer at 0x%08x",
                           (uint32_t)framebuffer.begin);

    /* --- 8. the memory-disk bridges --------------------------------------- */
    if (want_root) {
        md_bridge_config_t strategy;
        memset(&strategy, 0, sizeof strategy);
        strategy.read_site.pc        = request->md_read_site_pc;
        strategy.read_site.encoding  = S5L_BRINGUP_SVC_MD_READ;
        strategy.write_site.pc       = request->md_write_site_pc;
        strategy.write_site.encoding = S5L_BRINGUP_SVC_MD_WRITE;
        strategy.token_base          = S5L_BRINGUP_MD_TOKEN_BASE;
        strategy.media_size          = media_size;
        strategy.ram_base            = machine->ram_base;
        strategy.ram_size            = machine->ram_size;
        strategy.ram                 = machine->ram;
        strategy.block               = request->root_media;

        md_raw_bridge_config_t raw;
        memset(&raw, 0, sizeof raw);
        raw.site.pc                 = request->md_raw_site_pc;
        raw.site.encoding           = S5L_BRINGUP_SVC_RAW_ENTRY;
        /* The completion SVC is the second halfword of the same replaced
         * four-byte prologue; the bridge requires exactly site.pc + 2. */
        raw.completion_site.pc      = request->md_raw_site_pc + 2u;
        raw.completion_site.encoding = S5L_BRINGUP_SVC_RAW_DONE;
        raw.uiomove_thumb_pc        = request->uiomove_pc;
        raw.bounce_base_pa          = bounce.begin;
        raw.bounce_stride           = MD_RAW_BRIDGE_MAX_TRANSFER;
        raw.bounce_slot_count       = S5L_BRINGUP_MD_RAW_SLOT_COUNT;
        raw.expected_device         = S5L_BRINGUP_MD_RAW_DEVICE;
        raw.user_address_limit      = S5L_BRINGUP_USER_ADDRESS_LIMIT;
        raw.media_size              = media_size;
        raw.ram_base                = machine->ram_base;
        raw.ram_size                = machine->ram_size;
        raw.ram                     = machine->ram;
        raw.block                   = request->root_media;

        if (!md_bridge_config_valid(&strategy) ||
            !md_raw_bridge_config_valid(&raw))
            return result_fail(result, S5L_BRINGUP_ROOT_BRIDGE_REFUSED,
                               S5L_BRINGUP_STAGE_ROOT_BRIDGE,
                               "the guarded bridges rejected the planned "
                               "geometry (bounce 0x%08x, media %llu bytes)",
                               (uint32_t)bounce.begin,
                               (unsigned long long)media_size);

        md_bridge_init(&md->strategy, &strategy);
        md_raw_bridge_init(&md->raw, &raw);
        md->installed = true;
        arm_bus_set_privileged_svc_handler(&machine->bus, bringup_svc_handler,
                                           md);
        result->md_bridge_installed = true;
    }

    /* --- 9. point the CPU at the kernel ----------------------------------- */
    machine->cpu.r[15] = result->entry_pa;
    machine->cpu.r[0]  = result->boot_args_pa;   /* XNU takes boot_args in r0 */

    result->status = S5L_BRINGUP_OK;
    result->stage  = S5L_BRINGUP_STAGE_ENTRY;
    return S5L_BRINGUP_OK;
}
