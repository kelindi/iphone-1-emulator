#ifndef S5LBOX_BRINGUP_OS1_H
#define S5LBOX_BRINGUP_OS1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "soc.h"

typedef struct {
    const uint8_t *kernel; size_t kernel_size;
    const uint8_t *devicetree; size_t devicetree_size;
    const uint8_t *ramdisk; size_t ramdisk_size;
    const char *cmdline;
    bool framebuffer;
    uint32_t v_display;
    unsigned fb_width, fb_height;
    bool debug_enabled;
    /* Device-tree node paths (relative to root, e.g. "arm-io/usb-otg")
     * whose `compatible` is struck in the loaded copy so no driver matches. */
    const char *const *unmatch; unsigned unmatch_count;
} s5l_os1_request_t;

typedef struct {
    bool ok; char detail[256];
    uint32_t virt_base, phys_base, ram_size;
    uint32_t kernel_begin_pa, kernel_end_pa, entry_pa, entry_va;
    uint32_t devicetree_pa, devicetree_va, devicetree_size;
    uint32_t boot_args_pa, ramdisk_pa, ramdisk_va, ramdisk_size;
    uint32_t framebuffer_pa, vram_bytes, top_of_kernel_data_pa;
    char cmdline[256];
} s5l_os1_result_t;

bool s5l_os1_boot(s5l8900_t *m, const s5l_os1_request_t *req,
                  s5l_os1_result_t *res);

#endif
