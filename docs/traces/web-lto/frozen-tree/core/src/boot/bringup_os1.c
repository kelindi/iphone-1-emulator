#include "bringup_os1.h"
#include "bringup.h"
#include "macho.h"
#include "dt_patch.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define OS1_VIRT_BASE UINT32_C(0xc0000000)
#define PAGE 0x1000u
#define FB_ALIGN 0x4000u
#define RD_ALIGN 0x10000u
#define MIN_HEADROOM (24u << 20)

static bool align32(uint64_t v, uint32_t a, uint32_t *out) {
    uint64_t x = (v + a - 1u) & ~(uint64_t)(a - 1u);
    if (x > UINT32_MAX) return false;
    *out = (uint32_t)x; return true;
}

static bool in_ram(const s5l8900_t *m, uint64_t pa, uint64_t n) {
    return m && pa >= m->ram_base && pa <= (uint64_t)m->ram_base + m->ram_size &&
           n <= (uint64_t)m->ram_base + m->ram_size - pa;
}

static bool fail(s5l_os1_result_t *r, const char *fmt, ...) {
    va_list ap;
    r->ok = false;
    va_start(ap, fmt); vsnprintf(r->detail, sizeof r->detail, fmt, ap);
    va_end(ap);
    return false;
}

bool s5l_os1_boot(s5l8900_t *m, const s5l_os1_request_t *req,
                  s5l_os1_result_t *r) {
    if (!r) return false;
    memset(r, 0, sizeof *r);
    if (!m || !req || !req->kernel || !req->devicetree)
        return fail(r, "missing machine, kernel, or device tree");
    if (req->ramdisk_size && !req->ramdisk)
        return fail(r, "ramdisk size supplied without ramdisk data");
    r->virt_base = OS1_VIRT_BASE; r->phys_base = m->ram_base;
    r->ram_size = m->ram_size;
    if (req->devicetree_size > UINT32_MAX)
        return fail(r, "device tree is too large");

    macho_t image;
    macho_status_t ms = macho_parse(req->kernel, req->kernel_size, &image);
    if (ms != MACHO_OK)
        return fail(r, "Mach-O: %s", macho_strerror(ms));
    if (image.filetype != MH_EXECUTE || image.cputype != MH_CPU_TYPE_ARM)
        return fail(r, "Mach-O is not an ARM executable");
    if (!image.has_entry) return fail(r, "Mach-O has no entry point");
    uint32_t loaded_low = UINT32_MAX;
    for (unsigned i = 0; i < image.segment_count; i++) {
        const macho_segment_t *s = &image.segments[i];
        if (s->filesize && s->vmaddr >= OS1_VIRT_BASE &&
            s->vmaddr < loaded_low) loaded_low = s->vmaddr;
    }
    if (loaded_low == UINT32_MAX || image.vm_high < loaded_low)
        return fail(r, "kernel virtual span is below 0xc0000000");

    uint64_t dram_end = (uint64_t)m->ram_base + m->ram_size;
    uint64_t kb = m->ram_base + ((uint64_t)loaded_low - OS1_VIRT_BASE);
    uint64_t ke = m->ram_base + ((uint64_t)image.vm_high - OS1_VIRT_BASE);
    if (kb < m->ram_base || ke > dram_end || ke < kb)
        return fail(r, "kernel does not fit in DRAM");
    uint64_t entry = m->ram_base + ((uint64_t)image.entry - OS1_VIRT_BASE);
    if (entry < kb || entry >= ke)
        return fail(r, "entry is outside kernel span");
    r->kernel_begin_pa = (uint32_t)kb; r->kernel_end_pa = (uint32_t)ke;
    r->entry_va = image.entry; r->entry_pa = (uint32_t)entry;

    uint32_t tree_pa, args_pa, rd_pa = 0, fb_pa = 0, top;
    if (!align32(ke, PAGE, &tree_pa) ||
        !align32((uint64_t)tree_pa + req->devicetree_size, PAGE, &args_pa))
        return fail(r, "layout alignment overflow");
    uint64_t cursor = (uint64_t)args_pa + PAGE;
    uint32_t rd_size = 0;
    if (req->ramdisk_size) {
        if (req->ramdisk_size > UINT32_MAX ||
            !align32(cursor, RD_ALIGN, &rd_pa) ||
            !align32(req->ramdisk_size, PAGE, &rd_size))
            return fail(r, "ramdisk layout overflow");
        cursor = (uint64_t)rd_pa + rd_size;
    }
    if (req->framebuffer) {
        if (!req->fb_width || !req->fb_height ||
            !align32(cursor, FB_ALIGN, &fb_pa))
            return fail(r, "framebuffer layout overflow");
        cursor = (uint64_t)fb_pa + S5L_BRINGUP_VRAM_BYTES;
    }
    if (!align32(cursor, (uint32_t)S5L_BRINGUP_TOKD_ALIGNMENT, &top) ||
        !in_ram(m, top, MIN_HEADROOM))
        return fail(r, "static layout leaves less than 24 MB of DRAM");
    r->devicetree_pa = tree_pa;
    r->devicetree_va = OS1_VIRT_BASE + tree_pa - m->ram_base;
    r->devicetree_size = (uint32_t)req->devicetree_size;
    r->boot_args_pa = args_pa;
    r->ramdisk_pa = rd_pa; r->ramdisk_va = rd_pa ?
        OS1_VIRT_BASE + rd_pa - m->ram_base : 0;
    r->ramdisk_size = rd_size;
    r->framebuffer_pa = fb_pa;
    r->vram_bytes = req->framebuffer ? S5L_BRINGUP_VRAM_BYTES : 0;
    r->top_of_kernel_data_pa = top;

    for (unsigned i = 0; i < image.segment_count; i++) {
        const macho_segment_t *s = &image.segments[i];
        if (!s->filesize || s->vmaddr < OS1_VIRT_BASE) continue;
        uint64_t pa = m->ram_base + ((uint64_t)s->vmaddr - OS1_VIRT_BASE);
        if (!in_ram(m, pa, s->filesize))
            return fail(r, "kernel segment does not fit in DRAM");
        s5l8900_load(m, (uint32_t)pa, req->kernel + s->fileoff, s->filesize);
    }
    s5l8900_load(m, tree_pa, req->devicetree, req->devicetree_size);
    if (req->ramdisk_size) s5l8900_load(m, rd_pa, req->ramdisk, req->ramdisk_size);

    uint8_t *tree = m->ram + tree_pa - m->ram_base;
    static const struct { const char *path, *prop; uint32_t value; } clocks[] = {
        {"", "clock-frequency", 103000000u},
        {"cpus/cpu0", "timebase-frequency", 6000000u},
        {"cpus/cpu0", "clock-frequency", 412000000u},
        {"cpus/cpu0", "bus-frequency", 103000000u},
        {"cpus/cpu0", "memory-frequency", 103000000u},
        {"cpus/cpu0", "peripheral-frequency", 51500000u},
        {"cpus/cpu0", "fixed-frequency", 24000000u},
    };
    for (unsigned i = 0; i < sizeof clocks / sizeof clocks[0]; i++)
        if (!s5l_dt_set_u32(tree, req->devicetree_size, clocks[i].path,
                            clocks[i].prop, clocks[i].value))
            return fail(r, "missing DT clock property %s", clocks[i].prop);
    if (!s5l_dt_set_reg(tree, req->devicetree_size, "memory", "reg",
                        m->ram_base, m->ram_size))
        return fail(r, "missing memory reg");
    if (req->framebuffer &&
        !s5l_dt_set_reg(tree, req->devicetree_size, "vram", "reg",
                        fb_pa, S5L_BRINGUP_VRAM_BYTES))
        return fail(r, "missing vram reg");
    if (req->debug_enabled &&
        !s5l_dt_set_u32(tree, req->devicetree_size, "chosen",
                        "debug-enabled", 1u))
        return fail(r, "missing chosen debug-enabled");
    if (req->ramdisk &&
        !s5l_dt_memmap_add(tree, req->devicetree_size, "RAMDisk",
                           r->ramdisk_va, rd_size))
        return fail(r, "missing RAMDisk memory-map slot");
    for (unsigned i = 0; i < req->unmatch_count; i++) {
        const char *path = req->unmatch ? req->unmatch[i] : NULL;
        size_t node = path && *path ?
            s5l_dt_path(tree, req->devicetree_size, path) : DT_NO_NODE;
        uint32_t vl = 0;
        uint8_t *p = node == DT_NO_NODE ? NULL :
            s5l_dt_prop(tree, req->devicetree_size, node, "compatible", &vl);
        if (!p || !vl)
            return fail(r, "cannot un-match /%s", path ? path : "");
        p[0] = 'x';
    }

    uint8_t args[PAGE];
    memset(args, 0, sizeof args);
    args[0] = 1; args[2] = 1;
    s5l_dt_st32(args + 4, OS1_VIRT_BASE);
    s5l_dt_st32(args + 8, m->ram_base);
    s5l_dt_st32(args + 12, m->ram_size);
    s5l_dt_st32(args + 16, top);
    s5l_dt_st32(args + 20, fb_pa);
    s5l_dt_st32(args + 24, req->v_display);
    s5l_dt_st32(args + 28, req->fb_width * 4u);
    s5l_dt_st32(args + 32, req->fb_width);
    s5l_dt_st32(args + 36, req->fb_height);
    s5l_dt_st32(args + 40, 32u);
    s5l_dt_st32(args + 48, r->devicetree_va);
    s5l_dt_st32(args + 52, (uint32_t)req->devicetree_size);
    if (req->cmdline) {
        size_t n = strlen(req->cmdline);
        if (n > 255u) n = 255u;
        memcpy(args + 56, req->cmdline, n); args[56 + n] = '\0';
        memcpy(r->cmdline, req->cmdline, n); r->cmdline[n] = '\0';
    }
    s5l8900_load(m, args_pa, args, sizeof args);
    if (req->framebuffer &&
        !s5l_clcd_seed_window0(&m->clcd, fb_pa, req->fb_width,
                               req->fb_height, req->fb_width * 4u,
                               CLCD_FMT_32BPP, CLCD_ORDER_BGRA))
        return fail(r, "CLCD framebuffer seed refused");
    m->cpu.r[15] = r->entry_pa;
    m->cpu.r[0] = r->boot_args_pa;
    r->ok = true;
    return true;
}
