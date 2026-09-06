/*
 * S5LBox — bringing up the real iPhone OS 3.1.3 kernel.
 *
 * Three files are not a running machine. Between them sits a sequence that
 * tools/bootkernel.c has been performing on the desktop for ~90 recorded runs:
 * map the kernel's segments at the physical addresses its virtual addresses
 * imply, do the job iBoot would have done to the device tree, reserve the
 * boot-owned memory XNU must never see in its free-page pool, describe all of
 * it in a boot_args struct, and point the CPU at the entry vector.
 *
 * That sequence is what this interface is. It is deliberately portable C11
 * with no allocation, no file I/O and no platform types, for two reasons:
 * the iOS app must run exactly the same orchestration the desktop harness
 * does — not a re-derivation of it — and the arithmetic that decides where
 * every byte lands has to be checkable on a machine that has no phone
 * attached. core/tests/test_bringup.c runs it against the real firmware.
 *
 * WHAT THIS IS NOT. It is not a second bootkernel. bootkernel.c carries some
 * eighty diagnostic options, snapshotting, tracing, gesture injection and a
 * jailbreak payload; none of that is here. What is here is the one
 * configuration those runs converged on (run89-base), expressed as data.
 *
 * FIRMWARE-NEUTRALITY. core/include/md_bridge.h states the rule this file
 * follows: the portable core holds no firmware addresses. The two Thumb SVC
 * sites the memory-disk bridge replaces, and the identity check that proves
 * they are the bytes we think they are, live in tools/ios3_kernel_patch.c and
 * reach this module only through s5l_bringup_request_t::kernel_gate. Without
 * a gate there is no bridge and no root filesystem, and bring-up says so by
 * name rather than booting something that will panic later.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_BRINGUP_H
#define S5LBOX_BRINGUP_H

#include "macho.h"
#include "md_bridge.h"
#include "md_raw_bridge.h"
#include "soc.h"
#include "vm_block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * THE iPHONE1,2 / 7E18 NUMBERS.
 *
 * Every one of these is transcribed from tools/bootkernel.c rather than
 * derived here, and the run89-base header in docs/BOOTLOG.md is what says they
 * are the ones that boot. They are exposed rather than hidden so a test can
 * assert against the constant instead of against a copy of it.
 */
#define S5L_BRINGUP_VIRT_BASE      UINT32_C(0xc0000000)
#define S5L_BRINGUP_PHYS_BASE      UINT32_C(0x08000000)
#define S5L_BRINGUP_RAM_SIZE       UINT32_C(0x08000000)   /* 128 MB, as the hardware */

/* Panel geometry, as advertised to the kernel in Boot_Video. */
#define S5L_BRINGUP_FB_WIDTH       320u
#define S5L_BRINGUP_FB_HEIGHT      480u
#define S5L_BRINGUP_FB_BPP         4u
#define S5L_BRINGUP_FB_BYTES \
    (S5L_BRINGUP_FB_WIDTH * S5L_BRINGUP_FB_HEIGHT * S5L_BRINGUP_FB_BPP)

/*
 * /vram is the pool IOSurface allocates every surface from, NOT the scanout
 * buffer. Sizing it to one framebuffer is why run76 composited nothing; three
 * measured strictly worse than two in guest output (run86: 122 allocation
 * failures, ~1.4 KB LESS guest output, byte-identical frame).
 *
 * THE QUESTION IS OPEN, and an earlier version of this comment said the
 * opposite. run127 probed the region gate that refuses the second surface and
 * found that four surfaces make it return 1 where two make it return 0: the
 * second request is offset 0x12c000 + length 0x96000, and a two-surface pool
 * ends at exactly 0x12c000, so it begins one whole surface past the end. Three
 * is the observed minimum. What has NOT been shown is that removing that
 * refusal renders anything -- run127 reached the same console milestone as the
 * two-surface boots -- so the value stays at the one whose output is measured
 * until a run earns the change. tools/bootkernel.c reads THIS constant.
 *
 * 2026-07-30: A RUN EARNED THE CHANGE, and it took an interactive UI to do it.
 * Every earlier measurement was a boot that reached a STATIC screen, which asks
 * for two surfaces and stops. r190 unlocked the phone and tapped a button; the
 * transition that followed asked for a third and the guest said so seven times:
 *
 *     IOSurface warning: buffer allocation failed.
 *                        320 x 480 fmt: 42475241 size: 614400 bytes
 *
 * The compositor then had no backing store and the display lost its active RGB
 * window entirely -- the run ends with "running CLCD has no active RGB window"
 * where its no-tap control (r191, same checkpoint, same instruction count,
 * deterministic apart from the tap) still shows the screen. So a two-surface
 * pool is not merely conservative, it breaks the first UI animation that needs
 * a layer, and run86's "three is worse than two" was measured on a workload
 * that never asked for three.
 *
 * FOUR WAS NOT ENOUGH, and the failure counts say the pool was never sized for
 * an interactive machine at all. Counting "buffer allocation failed" in the
 * guest's own console across today's runs:
 *
 *     run170   drag that did NOT unlock                    1
 *     r181     unlocked                                   11
 *     r184/r189/r191  unlocked, NO TAP                     12
 *     r190/r188       unlocked + tap, 2 surfaces        19-20
 *     r193     unlocked + swipe, 4 surfaces               14
 *
 * Two things follow. The home screen ALONE starves the pool -- the no-tap
 * controls fail twelve times each, so this is not a tap-specific bug but a
 * continuous one, and on a real device it shows as the screen blinking as the
 * compositor loses and regains its backing store. And the display only DIES
 * around 19-20 failures: at 12 it still renders, so this is a threshold rather
 * than a cliff, which is why every static-screen measurement before today
 * missed it entirely.
 *
 * run86's "three measured worse than two" and run127's "three is the observed
 * minimum" were both taken on workloads that never asked for a third surface,
 * and neither generalises to a machine somebody is touching.
 *
 * SIXTEEN, and r194 is why. Cold boot, unlock, tap -- the exact sequence that
 * lost the display at two and at four:
 *
 *      2 surfaces   19-20 failures   display lost
 *      4 surfaces      14 failures   display lost
 *     16 surfaces       0 failures   renders, 238,252 bytes non-zero
 *
 * Zero, not merely fewer. The cost is 614,400 bytes of guest DRAM per surface
 * against 128 MB -- 9.8 MB at sixteen -- so the old caution was protecting
 * almost nothing and cost the first interactive screen.
 *
 * NOT the measured minimum: sixteen is the first value tried that reached zero,
 * and the true floor is somewhere in 5..16. Walking it down costs a cold boot
 * per candidate and buys back a few MB of a 128 MB machine, which is why it has
 * not been done. If DRAM ever gets tight, that is the experiment.
 */
#define S5L_BRINGUP_VRAM_SURFACES  16u
#define S5L_BRINGUP_VRAM_BYTES     (S5L_BRINGUP_FB_BYTES * S5L_BRINGUP_VRAM_SURFACES)

/*
 * topOfKernelData must be 16 KiB aligned, not merely page aligned: XNU derives
 * its ARMv6 L1 translation-table base from it and TTBR0[31:14] is that base.
 * A 4 KiB-aligned value produced a prefetch abort on the instruction right
 * after the MMU came on. The 0x11000 floor is the measured early-bootstrap
 * demand above the line (a 16 KiB L1 table plus what immediately follows it).
 */
#define S5L_BRINGUP_TOKD_ALIGNMENT       UINT64_C(0x4000)
#define S5L_BRINGUP_BOOTSTRAP_HEADROOM   UINT64_C(0x11000)

/*
 * The root filesystem is host-backed: the guest's /dev/md0 is a window onto a
 * file on the host, not a copy of it in guest DRAM. This synthetic base is the
 * 32-bit address published to the guest; nothing in DRAM lives there. The
 * audited iPhone OS 3 mdevstrategy path expands the page-number base and byte
 * offset into the split 64-bit bcopy_phys ABI, so token arithmetic may cross
 * 4 GiB even though the published base and device-tree length remain 32-bit.
 * Two GiB is a deliberate volume cap, not guest RAM.
 */
#define S5L_BRINGUP_MD_TOKEN_BASE        UINT64_C(0xe0000000)
#define S5L_BRINGUP_MD_MAX_SIZE          UINT64_C(0x80000000)
#define S5L_BRINGUP_MD_RAW_SLOT_COUNT    UINT32_C(4)
#define S5L_BRINGUP_MD_RAW_RESERVE_SIZE \
    (S5L_BRINGUP_MD_RAW_SLOT_COUNT * MD_RAW_BRIDGE_MAX_TRANSFER)

/* boot_args carries the command line in a 255-byte field plus a terminator. */
#define S5L_BRINGUP_CMDLINE_CAPACITY  256u
#define S5L_BRINGUP_DETAIL_CAPACITY   192u

/*
 * The default command line, and why each token is on it.
 *
 *   debug=0x8            keeps the kernel talking after a fault instead of
 *                        resetting the device.
 *   serial=1             routes cnputc to the UART. Without it the only
 *                        console is the video one.
 *   nand-enable-adm=0    MANDATORY. AppleS5L8900XADMFMC::start polls a NAND
 *                        DMA ready bit this machine has no NAND to raise, and
 *                        the boot dies there; the driver's own probe() honours
 *                        this argument. See docs/BOOTLOG.md.
 *
 * "rd=md0" is appended by bring-up when a root medium is supplied, because
 * IOFindBSDRoot compares rdBootVar[0..1] against "md" and rdBootVar[3] against
 * NUL: the token has to be exactly "md<digit>".
 */
#define S5L_BRINGUP_DEFAULT_CMDLINE  "debug=0x8 serial=1 nand-enable-adm=0"
#define S5L_BRINGUP_ROOT_TOKEN       "rd=md0"

typedef enum {
    S5L_BRINGUP_OK = 0,
    S5L_BRINGUP_INVALID_ARGUMENT,
    S5L_BRINGUP_KERNEL_MISSING,        /* no kernel bytes supplied            */
    S5L_BRINGUP_KERNEL_MALFORMED,      /* macho_parse rejected it; see        */
                                       /* result->macho_status               */
    S5L_BRINGUP_KERNEL_NOT_EXECUTABLE, /* parsed, but not an ARM MH_EXECUTE   */
    S5L_BRINGUP_KERNEL_NO_ENTRY,       /* no LC_UNIXTHREAD                    */
    S5L_BRINGUP_KERNEL_BELOW_VIRT_BASE,
    S5L_BRINGUP_KERNEL_SPAN_UNSAFE,    /* a segment does not fit in DRAM      */
    S5L_BRINGUP_ENTRY_OUTSIDE_KERNEL,
    S5L_BRINGUP_DEVICETREE_MISSING,
    S5L_BRINGUP_DEVICETREE_MALFORMED,  /* not Apple's flat format, or         */
                                       /* truncated part-way through          */
    S5L_BRINGUP_DEVICETREE_TOO_LARGE,  /* exceeds the 32-bit boot_args field  */
    S5L_BRINGUP_DEVICETREE_PATCH_FAILED, /* a required property was absent or */
                                       /* the wrong length; see result->detail */
    S5L_BRINGUP_LAYOUT_OVERFLOW,       /* an address computation would wrap   */
    S5L_BRINGUP_LAYOUT_NO_ROOM,        /* the pieces do not fit in DRAM       */
    S5L_BRINGUP_LAYOUT_OVERLAP,
    S5L_BRINGUP_CMDLINE_TOO_LONG,      /* over the 255-byte boot_args field   */
    S5L_BRINGUP_FRAMEBUFFER_REFUSED,   /* CLCD rejected the validated seed    */
    S5L_BRINGUP_ROOT_MEDIA_INVALID,    /* size zero, over the token window,   */
                                       /* or an incomplete vm_block_t         */
    S5L_BRINGUP_ROOT_GATE_MISSING,     /* root media without a kernel gate:   */
                                       /* the SVC sites would never exist     */
    S5L_BRINGUP_ROOT_GATE_REFUSED,     /* the gate rejected this kernel       */
    S5L_BRINGUP_ROOT_BRIDGE_REFUSED,   /* the bridges rejected the geometry   */
    S5L_BRINGUP_ROOT_STORAGE_MISSING   /* nowhere to hand md storage back     */
} s5l_bringup_status_t;

/*
 * Where it stopped. Reported separately from the status because the same
 * refusal means different things at different points -- an overlap found
 * while planning is a configuration error, one found after the kernel is
 * resident is a bug in this file.
 */
typedef enum {
    S5L_BRINGUP_STAGE_NONE = 0,
    S5L_BRINGUP_STAGE_ARGUMENTS,
    S5L_BRINGUP_STAGE_KERNEL_PARSE,
    S5L_BRINGUP_STAGE_LAYOUT,
    S5L_BRINGUP_STAGE_KERNEL_LOAD,
    S5L_BRINGUP_STAGE_KERNEL_GATE,
    S5L_BRINGUP_STAGE_DEVICETREE_LOAD,
    S5L_BRINGUP_STAGE_DEVICETREE_PATCH,
    S5L_BRINGUP_STAGE_COMMAND_LINE,
    S5L_BRINGUP_STAGE_BOOT_ARGS,
    S5L_BRINGUP_STAGE_FRAMEBUFFER,
    S5L_BRINGUP_STAGE_ROOT_BRIDGE,
    S5L_BRINGUP_STAGE_ENTRY
} s5l_bringup_stage_t;

/*
 * Authorize and prepare the kernel image that is already resident in guest
 * RAM. This is where a frontend proves the bytes are the exact build it has
 * patch offsets for, and installs them -- tools/ios3_kernel_patch.c is the
 * implementation the desktop harness and the app both use.
 *
 * It runs AFTER the segments are loaded and BEFORE anything else touches the
 * machine, so the gate sees both the file and the loaded image and can compare
 * them. Returning false stops bring-up with S5L_BRINGUP_ROOT_GATE_REFUSED and
 * leaves the machine unstarted; `detail` (never NULL, always NUL-terminable)
 * is copied into the result so the reason reaches a user.
 */
typedef bool (*s5l_bringup_kernel_gate_fn)(void *context,
                                           const uint8_t *kernel_file,
                                           size_t kernel_file_size,
                                           uint8_t *ram,
                                           size_t ram_size,
                                           uint64_t ram_base,
                                           uint32_t virt_base,
                                           char *detail,
                                           size_t detail_capacity);

/*
 * A zero-initialized request asks for exactly the run89-base configuration:
 * framebuffer on, /vram two surfaces, lcd-panel-id patched, /memory published,
 * the default command line. Every flag is therefore phrased as an opt-OUT, so
 * a caller who forgets a field gets the configuration that boots rather than
 * a silently degraded one.
 */
typedef struct {
    /* The decrypted kernelcache, as a plain 32-bit Mach-O. Borrowed for the
     * duration of the call only; segments are copied into guest DRAM. */
    const uint8_t *kernel;
    size_t         kernel_size;

    /* The flattened Apple device tree from the IPSW. Copied into guest DRAM
     * and patched THERE: the caller's buffer is never modified. */
    const uint8_t *devicetree;
    size_t         devicetree_size;

    /*
     * The root filesystem, as a writable host-backed block device -- the WORK
     * image, never the pristine import. NULL boots without a root filesystem,
     * which is a legitimate diagnostic but ends in a kernel panic looking for
     * one; bring-up succeeds and result->md_bridge_installed says so.
     *
     * Must remain alive and stable for as long as the machine runs.
     */
    const vm_block_t *root_media;

    /* Required whenever root_media is set; see s5l_bringup_kernel_gate_fn. */
    s5l_bringup_kernel_gate_fn kernel_gate;
    void                      *kernel_gate_context;

    /*
     * WHERE THE GATE PUT THE SVC SITES. Required with root_media, ignored
     * without it.
     *
     * These are firmware addresses, and md_bridge.h's rule is that the
     * portable core holds none: the frontend that owns the patch manifest
     * owns these too, and passes them here so the bridges watch exactly the
     * halfwords the gate replaced. For iPhone OS 3.1.3 7E18 they are
     * IOS3_KERNEL_PATCH_MD_READ_VA, _MD_WRITE_VA, _RAW_WATCHER_VA and
     * IOS3_KERNEL_UIOMOVE_VA from tools/ios3_kernel_patch.h.
     *
     * The raw bridge additionally requires its completion site to be
     * md_raw_site_pc + 2 -- the second halfword of the same replaced four-byte
     * prologue -- so bring-up derives that rather than taking it.
     */
    uint32_t md_read_site_pc;
    uint32_t md_write_site_pc;
    uint32_t md_raw_site_pc;
    uint32_t uiomove_pc;

    /* NULL selects S5L_BRINGUP_DEFAULT_CMDLINE. "rd=md0" is appended when a
     * root medium is supplied and the caller has not already named one. */
    const char *cmdline;

    /* Opt-outs. See the note above: zero means "do the thing that boots". */
    bool no_framebuffer;      /* leave Boot_Video and /vram unconfigured  */
    bool no_lcd_panel_id;     /* A/B control for the Merlot panel ID      */
    bool no_memory_node;      /* leave /memory reg at the template's zero */

    /*
     * iPhone OS 3's two-part code-signing policy input. When true, bring-up
     * sets /chosen/debug-enabled to 1 and appends the three established
     * cs_enforcement_disable/amfi boot arguments unless the caller already
     * supplied a value for a key. This changes guest policy only; it is not
     * evidence that an unsigned binary executed successfully.
     */
    bool guest_codesign_disabled;

    /*
     * DEVICE-TREE NODES TO UN-MATCH, by path, before the tree is published.
     *
     * Un-matching is how this project says "this machine does not have that
     * peripheral". Writing 'x' over the first byte of `compatible` leaves the
     * node, its phandle and every reference to it exactly where they were, and
     * only stops IOKit finding a driver for it. Nothing is deleted, so nothing
     * that indexes the tree by offset or phandle can be thrown off.
     *
     * It is not cosmetic, and these are not hypothetical. /arm-io/mbx left
     * matched is the difference between a boot and a hang: the PowerVR driver
     * busy-polls a reset bit in a register block this VM does not model. So is
     * /arm-io/sha1, whose hardware hook sends every exactly-4096-byte digest --
     * the size cs_validate_page asks for -- to a register file we do not model,
     * after which launchd's first text page fails its signature and the boot
     * spins on cs_invalid_page forever, having printed nothing.
     *
     * A path that is absent, or whose node carries no `compatible`, FAILS the
     * bring-up rather than being skipped. A caller that asked for a device to
     * be absent and silently got it present would go on to debug a machine
     * that is not the one it thinks it configured.
     */
    const char *const *unmatch;
    unsigned unmatch_count;

    /*
     * boot_args Revision and Version. Version 6 is MEASURED, not guessed:
     * pe_identify_machine() rejects anything else on this kernel. Zero here
     * means "the default", so a zeroed request is still correct.
     */
    uint16_t boot_args_revision;   /* 0 -> 1 */
    uint16_t boot_args_version;    /* 0 -> 6 */

    /*
     * Boot_Video v_display selects the console MODE, and it is not "is there
     * a display". _PE_create_console branches on it: non-zero picks
     * kPEGraphicsMode and _vcattach then returns immediately, so the kernel
     * text console is never acquired and nothing is drawn. Zero picks
     * kPETextMode and the kernel paints its boot log into the framebuffer.
     * Zero is the default for that reason.
     */
    uint32_t v_display;
} s5l_bringup_request_t;

/*
 * Storage for the two memory-disk bridges and the SVC multiplexer that fans
 * one privileged-SVC callback out to them. Roughly 300 KiB -- md_raw_bridge_t
 * carries two 128 KiB buffers -- so it is CALLER-OWNED and must be in static
 * or heap storage, never on a small iOS thread stack, and must outlive the
 * machine. Contents are opaque; bring-up initializes every field it uses.
 */
typedef struct {
    md_bridge_t     strategy;
    md_raw_bridge_t raw;
    bool            installed;
} s5l_bringup_md_t;

/*
 * What was actually planned and done -- not what was asked for. Everything
 * here is filled in on success; on failure the fields reached before the
 * refusal are still populated, because "it got as far as X" is the useful
 * half of a failed bring-up.
 */
typedef struct {
    s5l_bringup_status_t status;
    s5l_bringup_stage_t  stage;

    uint32_t virt_base;
    uint32_t phys_base;
    uint32_t ram_size;

    uint32_t kernel_begin_pa;      /* vm_low  mapped down                    */
    uint32_t kernel_end_pa;        /* vm_high mapped down                    */
    uint32_t entry_va;
    uint32_t entry_pa;
    unsigned segments_loaded;      /* file-backed segments copied into DRAM  */

    uint32_t devicetree_pa;
    uint32_t devicetree_va;        /* what boot_args publishes: VIRTUAL      */
    uint32_t devicetree_size;
    unsigned devicetree_patches;   /* properties actually rewritten          */
    unsigned devicetree_unmatched; /* nodes whose compatible was struck out  */

    uint32_t boot_args_pa;
    uint32_t raw_bounce_pa;        /* faultable-uiomove bounce reservation   */
    uint32_t framebuffer_pa;       /* base of the /vram pool                 */
    uint32_t vram_bytes;
    uint32_t top_of_kernel_data_pa;
    uint32_t top_of_kernel_data_va;
    uint32_t free_pool_bytes;      /* [topOfKernelData, end of DRAM)         */

    uint64_t root_media_size;
    uint32_t root_dt_address;      /* what /chosen/memory-map:RAMDisk names  */
    uint32_t root_dt_size;

    bool framebuffer_enabled;
    bool md_bridge_installed;

    /* Only meaningful when status is S5L_BRINGUP_KERNEL_MALFORMED. */
    macho_status_t macho_status;

    /* The exact bytes handed to the kernel, terminator included. */
    char cmdline[S5L_BRINGUP_CMDLINE_CAPACITY];

    /* One line naming what went wrong, safe to show a user. Empty on OK. */
    char detail[S5L_BRINGUP_DETAIL_CAPACITY];
} s5l_bringup_result_t;

/*
 * Prepare `machine` to execute the supplied kernel, and return whether it
 * worked.
 *
 * `machine` must already be s5l8900_init()'d at S5L_BRINGUP_PHYS_BASE with
 * S5L_BRINGUP_RAM_SIZE bytes of DRAM, and must not be running. `md` may be
 * NULL only when request->root_media is NULL. `result` is required and is
 * fully reset before anything else happens.
 *
 * On success the CPU's PC is the kernel entry, r0 is the boot_args physical
 * address, and s5l8900_run() will execute the kernel. On failure the machine
 * is left un-started; guest DRAM may have been partially written, so the
 * caller should free and rebuild it rather than fall back on the same object.
 *
 * Performs no allocation and touches no file. Not reentrant.
 */
s5l_bringup_status_t s5l_bringup(s5l8900_t *machine,
                                 const s5l_bringup_request_t *request,
                                 s5l_bringup_md_t *md,
                                 s5l_bringup_result_t *result);

/* Stable machine-readable names, for logs and for the app's status line. */
const char *s5l_bringup_status_name(s5l_bringup_status_t status);
const char *s5l_bringup_stage_name(s5l_bringup_stage_t stage);

#endif /* S5LBOX_BRINGUP_H */
