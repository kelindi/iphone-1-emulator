/*
 * S5LBox — PC-to-symbol resolution for the XNU kernelcache.
 *
 * A wedged boot reports a hot PC. Without this, that PC is a bare hex number,
 * and every diagnosis cycle starts by working out by hand which kext owns it —
 * a cost we have paid four times over (ADMFMC, MBX, IORTC, ...). This module
 * turns an address into a name, from the two sources a 3.x kernelcache carries:
 *
 *   1. LC_SYMTAB — the kernel's own nlist table. xnu is NOT stripped in this
 *      build (11430 N_SECT symbols), so every address inside the kernel proper
 *      resolves to "_symbol+0x12".
 *
 *   2. __PRELINK_INFO — an XML plist array of one dict per prelinked kext.
 *      MEASURED against iPhone1,2 7E18 (xnu-1357.5.30): the keys that matter
 *      are CFBundleIdentifier, _PrelinkExecutable (load address) and
 *      _PrelinkExecutableSize. Note the names: this era has NO
 *      _PrelinkExecutableLoadAddr — that spelling arrives later — which is
 *      exactly why the parser refuses to guess and reports what it did find.
 *
 * WHAT IS NOT AVAILABLE, STATED: the prelinked kexts carry no symbol table.
 * Each one is a real MH_KEXT_BUNDLE Mach-O inside __PRELINK_TEXT, but its load
 * commands are only __TEXT, __DATA and LC_UUID — the kernelcache builder
 * stripped LC_SYMTAB. So a kext address resolves to "<bundle-id>+0xNNNN" and
 * cannot resolve further. If a future image does carry per-kext symbols, the
 * hook is kext_t.addr/size plus a per-kext table in ksyms_resolve().
 *
 * Symbol NAMES point into the caller's image buffer and are not copied; the
 * image must outlive the ksyms_t.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_KSYMS_H
#define S5LBOX_KSYMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KSYMS_MAX_KEXTS   256u
#define KSYMS_MAX_IDS     1024u   /* plist ID="n" back-references          */
#define KSYMS_BUNDLE_MAX   96u    /* longest identifier seen is ~46 chars  */

/* How far past a symbol we still consider ourselves "inside" it. Beyond this
 * the address is more likely to be data or a hole than a huge function. */
#define KSYMS_MAX_SYM_SPAN 0x8000u

typedef struct {
    uint32_t    value;
    const char *name;             /* into the image buffer, not owned */
} ksym_t;

typedef struct {
    char     bundle[KSYMS_BUNDLE_MAX];   /* CFBundleIdentifier            */
    uint32_t addr;                       /* _PrelinkExecutable            */
    uint32_t size;                       /* _PrelinkExecutableSize        */
    uint32_t kmod_info;                  /* _PrelinkKmodInfo, 0 if absent */
    bool     has_exec;                   /* false for KPI pseudo-extensions */
} kext_t;

typedef enum {
    KSYMS_OK = 0,
    KSYMS_ERR_MACHO,        /* the image is not a Mach-O we can parse       */
    KSYMS_ERR_NO_SYMTAB,    /* no LC_SYMTAB: kernel symbols unavailable     */
    KSYMS_ERR_NO_PRELINK,   /* no __PRELINK_INFO: not a kernelcache         */
    KSYMS_ERR_PLIST,        /* __PRELINK_INFO is not shaped as expected     */
    KSYMS_ERR_TOO_MANY,     /* more kexts / plist IDs than we track         */
    KSYMS_ERR_NOMEM
} ksyms_status_t;

typedef struct {
    const uint8_t *img;
    size_t         len;

    /* kernel symbols, sorted by value */
    ksym_t        *sym;
    unsigned       nsym;
    ksyms_status_t sym_status;

    /* prelinked kext load map */
    kext_t         kext[KSYMS_MAX_KEXTS];
    unsigned       nkext;         /* entries with an executable come first? no: source order */
    unsigned       nkext_exec;    /* how many of those actually have code   */
    uint32_t       prelink_lo, prelink_hi;   /* __PRELINK_TEXT extent       */
    ksyms_status_t prelink_status;
    /*
     * Why the prelink map is missing / suspect, in the parser's own words.
     * This is the whole point of failing loudly: "expected <array> at the
     * start of __PRELINK_INFO, found '<plist'" is a fix; a silent empty map
     * is another wasted diagnosis cycle.
     */
    char           detail[256];
} ksyms_t;

/*
 * Parse both sources out of `img`. Never fails as a whole: a missing symbol
 * table or an unrecognised plist is recorded in sym_status / prelink_status
 * (with `detail` filled in) so the caller can shout about it and carry on with
 * whatever did work. The return value is the more severe of the two, or
 * KSYMS_ERR_MACHO if the image is not parseable at all.
 */
ksyms_status_t ksyms_load(ksyms_t *ks, const uint8_t *img, size_t len);
void           ksyms_free(ksyms_t *ks);

/* Which kext owns this address, or NULL. Thumb bit is ignored. */
const kext_t  *ksyms_kext_at(const ksyms_t *ks, uint32_t addr);

/*
 * Name an address into `buf` (returned for printf convenience):
 *   "_IOFindBSDRoot+0x2a"                  kernel text
 *   "com.apple.driver.AppleMBX+0x1234"     prelinked kext
 *   "?"                                    nothing owns it
 */
const char    *ksyms_resolve(const ksyms_t *ks, uint32_t addr,
                             char *buf, size_t bufsz);

/* Exact value of a named kernel symbol, or 0. */
uint32_t       ksyms_value(const ksyms_t *ks, const char *name);

const char    *ksyms_strerror(ksyms_status_t st);

#endif /* S5LBOX_KSYMS_H */
