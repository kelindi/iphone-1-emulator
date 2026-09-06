/*
 * S5LBox — PC-to-symbol resolution: kernel nlist symbols + the prelinked
 * kext load map. See ksyms.h for what this exists to stop us re-doing by hand.
 *
 * The plist scanner here is deliberately NOT a general XML parser. It walks
 * __PRELINK_INFO once, tracking container nesting, and only looks at keys
 * directly inside the dicts of the root array. What it does do is refuse to
 * proceed on anything it does not recognise: an unexpected root element, a
 * kext with a load address outside __PRELINK_TEXT, an IDREF with no matching
 * ID, a bundle identifier with characters an identifier cannot contain. Each
 * of those writes a sentence into ks->detail saying what it saw and where, so
 * the failure is a fix rather than another diagnosis cycle.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "ksyms.h"
#include "macho.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const char *ksyms_strerror(ksyms_status_t st) {
    switch (st) {
        case KSYMS_OK:            return "ok";
        case KSYMS_ERR_MACHO:     return "the image is not a Mach-O we can parse";
        case KSYMS_ERR_NO_SYMTAB: return "no LC_SYMTAB — the image is stripped";
        case KSYMS_ERR_NO_PRELINK:return "no __PRELINK_INFO — not a kernelcache";
        case KSYMS_ERR_PLIST:     return "__PRELINK_INFO is not shaped as expected";
        case KSYMS_ERR_TOO_MANY:  return "more kexts or plist IDs than we track";
        case KSYMS_ERR_NOMEM:     return "out of memory";
        default:                  return "unknown error";
    }
}

/* ===========================================================================
 * 1. LC_SYMTAB — the kernel's own symbols.
 * ========================================================================= */

static int ksym_cmp(const void *a, const void *b) {
    uint32_t x = ((const ksym_t *)a)->value, y = ((const ksym_t *)b)->value;
    return x < y ? -1 : x > y ? 1 : 0;
}

static ksyms_status_t symtab_load(ksyms_t *ks, const macho_t *m) {
    if (!m->has_symtab || !m->nsyms) return KSYMS_ERR_NO_SYMTAB;

    ks->sym = calloc(m->nsyms, sizeof *ks->sym);
    if (!ks->sym) return KSYMS_ERR_NOMEM;

    for (uint32_t k = 0; k < m->nsyms; k++) {
        const uint8_t *e = ks->img + m->symoff + (size_t)k * 12;
        uint32_t strx = rd32(e);
        uint8_t  type = e[4];
        uint32_t val  = rd32(e + 8);
        if ((type & 0x0e) != 0x0e || !val) continue;      /* N_SECT only */
        if (strx >= m->strsize) continue;
        const char *name = (const char *)(ks->img + m->stroff + strx);
        if (!memchr(name, '\0', m->strsize - strx)) continue;
        ks->sym[ks->nsym].value = val;
        ks->sym[ks->nsym].name  = name;
        ks->nsym++;
    }
    qsort(ks->sym, ks->nsym, sizeof *ks->sym, ksym_cmp);
    return ks->nsym ? KSYMS_OK : KSYMS_ERR_NO_SYMTAB;
}

/* ===========================================================================
 * 2. __PRELINK_INFO — the kext load map.
 * ========================================================================= */

typedef struct {
    ksyms_t    *ks;
    const char *base, *end;               /* the whole plist text          */
    struct { unsigned id; const char *txt; unsigned len; } id[KSYMS_MAX_IDS];
    unsigned    nid;
} pl_t;

static ksyms_status_t plfail(pl_t *pl, const char *at,
                             ksyms_status_t st, const char *fmt, ...) {
    char msg[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(pl->ks->detail, sizeof pl->ks->detail,
             "__PRELINK_INFO+0x%x: %s", (unsigned)(at - pl->base), msg);
    return st;
}

typedef struct {
    const char *name; unsigned namelen;
    bool        closing, selfclose;
    bool        has_id, has_idref;
    unsigned    id, idref;
    const char *after;                    /* just past '>' */
} tag_t;

static bool name_is(const tag_t *t, const char *s) {
    return strlen(s) == t->namelen && !memcmp(t->name, s, t->namelen);
}

static bool is_namechar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':';
}

/* Parse the tag starting at p (which must be '<'). */
static ksyms_status_t read_tag(pl_t *pl, const char *p, tag_t *t) {
    memset(t, 0, sizeof *t);
    const char *q = memchr(p, '>', (size_t)(pl->end - p));
    if (!q) return plfail(pl, p, KSYMS_ERR_PLIST, "unterminated tag");
    t->after     = q + 1;
    t->selfclose = q > p && q[-1] == '/';

    const char *s = p + 1;
    if (s < q && *s == '/') { t->closing = true; s++; }
    t->name = s;
    while (s < q && is_namechar(*s)) s++;
    t->namelen = (unsigned)(s - t->name);
    if (!t->namelen)
        return plfail(pl, p, KSYMS_ERR_PLIST, "tag with no element name");

    /* Attributes: name="value", space separated. Only ID/IDREF matter; any
     * other (size=, version=) is skipped, but a malformed one is fatal — we
     * would otherwise be guessing at where the value ends. */
    while (s < q) {
        while (s < q && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
        if (s >= q || *s == '/') break;
        const char *an = s;
        while (s < q && is_namechar(*s)) s++;
        unsigned anlen = (unsigned)(s - an);
        if (!anlen || s >= q || *s != '=')
            return plfail(pl, p, KSYMS_ERR_PLIST, "malformed attribute");
        s++;
        if (s >= q || *s != '"')
            return plfail(pl, p, KSYMS_ERR_PLIST, "attribute value is not quoted");
        s++;
        const char *av = s;
        while (s < q && *s != '"') s++;
        if (s >= q)
            return plfail(pl, p, KSYMS_ERR_PLIST, "unterminated attribute value");
        unsigned avlen = (unsigned)(s - av);
        s++;
        unsigned n = 0;
        bool numeric = avlen > 0;
        for (unsigned i = 0; i < avlen; i++) {
            if (av[i] < '0' || av[i] > '9') { numeric = false; break; }
            n = n * 10 + (unsigned)(av[i] - '0');
        }
        if (anlen == 2 && !memcmp(an, "ID", 2)) {
            if (!numeric)
                return plfail(pl, p, KSYMS_ERR_PLIST, "ID= is not a number");
            t->has_id = true; t->id = n;
        } else if (anlen == 5 && !memcmp(an, "IDREF", 5)) {
            if (!numeric)
                return plfail(pl, p, KSYMS_ERR_PLIST, "IDREF= is not a number");
            t->has_idref = true; t->idref = n;
        }
    }
    return KSYMS_OK;
}

static ksyms_status_t id_put(pl_t *pl, const char *at, unsigned id,
                             const char *txt, unsigned len) {
    if (pl->nid >= KSYMS_MAX_IDS)
        return plfail(pl, at, KSYMS_ERR_TOO_MANY,
                      "more than %u plist ID= back-references", KSYMS_MAX_IDS);
    pl->id[pl->nid].id = id; pl->id[pl->nid].txt = txt; pl->id[pl->nid].len = len;
    pl->nid++;
    return KSYMS_OK;
}

static ksyms_status_t id_get(pl_t *pl, const char *at, unsigned id,
                             const char **txt, unsigned *len) {
    for (unsigned i = 0; i < pl->nid; i++)
        if (pl->id[i].id == id) { *txt = pl->id[i].txt; *len = pl->id[i].len; return KSYMS_OK; }
    return plfail(pl, at, KSYMS_ERR_PLIST,
                  "IDREF=\"%u\" with no matching ID= earlier in the plist", id);
}

/* "0x1234" or "1234" -> value. Fails rather than truncating. */
static ksyms_status_t parse_int(pl_t *pl, const char *at,
                                const char *txt, unsigned len, uint32_t *out) {
    uint64_t v = 0;
    unsigned i = 0, base = 10;
    if (len > 2 && txt[0] == '0' && (txt[1] == 'x' || txt[1] == 'X')) { base = 16; i = 2; }
    if (i >= len)
        return plfail(pl, at, KSYMS_ERR_PLIST, "empty <integer>");
    for (; i < len; i++) {
        char c = txt[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return plfail(pl, at, KSYMS_ERR_PLIST,
                           "<integer> contains '%c', not a base-%u digit", c, base);
        v = v * base + d;
        if (v > 0xffffffffull)
            return plfail(pl, at, KSYMS_ERR_PLIST,
                          "<integer> does not fit in 32 bits — this is a 32-bit "
                          "kernelcache, so the load map cannot be what we think");
    }
    *out = (uint32_t)v;
    return KSYMS_OK;
}

/* Sort: kexts with code first, by load address; the rest by source order. */
static int kext_cmp(const void *a, const void *b) {
    const kext_t *x = a, *y = b;
    if (x->has_exec != y->has_exec) return x->has_exec ? -1 : 1;
    if (x->addr != y->addr) return x->addr < y->addr ? -1 : 1;
    return 0;
}

static ksyms_status_t prelink_load(ksyms_t *ks, const macho_t *m) {
    const macho_segment_t *info = macho_segment(m, "__PRELINK_INFO");
    const macho_segment_t *text = macho_segment(m, "__PRELINK_TEXT");
    if (!info || !info->filesize) {
        snprintf(ks->detail, sizeof ks->detail,
                 "the image has no __PRELINK_INFO segment, so it carries no "
                 "prelinked kexts (a plain kernel, not a kernelcache)");
        return KSYMS_ERR_NO_PRELINK;
    }
    if (!text || !text->vmsize) {
        snprintf(ks->detail, sizeof ks->detail,
                 "__PRELINK_INFO is present but __PRELINK_TEXT is missing or "
                 "empty — refusing to place kexts at unverifiable addresses");
        return KSYMS_ERR_PLIST;
    }
    ks->prelink_lo = text->vmaddr;
    ks->prelink_hi = text->vmaddr + text->vmsize;

    /* The segment is NUL-padded to a page; the plist ends at the first NUL. */
    const char *p   = (const char *)(ks->img + info->fileoff);
    const char *cap = p + info->filesize;
    const char *end = p;
    while (end < cap && *end) end++;

    pl_t pl;
    memset(&pl, 0, sizeof pl);
    pl.ks = ks; pl.base = p; pl.end = end;

    char     stack[64];
    unsigned sp = 0;
    bool     root_seen = false;
    unsigned root_sp = 0;

    kext_t   cur;
    bool     in_kext = false, got_exec = false, got_size = false;
    char     pending[64];
    bool     has_pending = false;
    memset(&cur, 0, sizeof cur);
    pending[0] = '\0';

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p >= end) break;
        if (*p != '<')
            return plfail(&pl, p, KSYMS_ERR_PLIST,
                          "expected a tag, found character '%c' — text outside "
                          "an element", *p);
        /* <?xml ... ?> and <!DOCTYPE ...>: legal wrappers, nothing to learn. */
        if (p + 1 < end && p[1] == '?') {
            const char *q = memchr(p, '>', (size_t)(end - p));
            if (!q) return plfail(&pl, p, KSYMS_ERR_PLIST, "unterminated <? ?>");
            p = q + 1; continue;
        }
        if (p + 1 < end && p[1] == '!') {
            if (end - p > 4 && !memcmp(p, "<!--", 4))
                return plfail(&pl, p, KSYMS_ERR_PLIST,
                              "XML comment — this scanner does not handle them");
            const char *q = memchr(p, '>', (size_t)(end - p));
            if (!q) return plfail(&pl, p, KSYMS_ERR_PLIST, "unterminated <! >");
            p = q + 1; continue;
        }

        tag_t t;
        ksyms_status_t st = read_tag(&pl, p, &t);
        if (st != KSYMS_OK) return st;
        const char *tag_at = p;

        if (t.closing) {
            if (!sp)
                return plfail(&pl, p, KSYMS_ERR_PLIST, "</%.*s> with nothing open",
                              (int)t.namelen, t.name);
            char want = stack[--sp];
            const char *wname = want == 'd' ? "dict" : want == 'a' ? "array" : "plist";
            if (!name_is(&t, wname))
                return plfail(&pl, p, KSYMS_ERR_PLIST,
                              "</%.*s> closes a <%s>", (int)t.namelen, t.name,
                              wname);
            if (in_kext && sp == root_sp) {
                /* ---- end of one kext dict: validate and file it ---- */
                if (!cur.bundle[0])
                    return plfail(&pl, p, KSYMS_ERR_PLIST,
                                  "a dict in the root array has no "
                                  "CFBundleIdentifier — the plist is not the "
                                  "flat array of kext dicts we assume");
                if (got_exec != got_size)
                    return plfail(&pl, p, KSYMS_ERR_PLIST,
                                  "%s has _PrelinkExecutable%s but not the other "
                                  "— cannot bound its text", cur.bundle,
                                  got_exec ? "" : "Size");
                cur.has_exec = got_exec && got_size && cur.size != 0;
                if (cur.has_exec) {
                    uint64_t hi = (uint64_t)cur.addr + cur.size;
                    if (cur.addr < ks->prelink_lo || hi > (uint64_t)ks->prelink_hi)
                        return plfail(&pl, p, KSYMS_ERR_PLIST,
                                      "%s is at 0x%08x..0x%08x, outside "
                                      "__PRELINK_TEXT 0x%08x..0x%08x",
                                      cur.bundle, cur.addr, (uint32_t)hi,
                                      ks->prelink_lo, ks->prelink_hi);
                    ks->nkext_exec++;
                }
                if (ks->nkext >= KSYMS_MAX_KEXTS)
                    return plfail(&pl, p, KSYMS_ERR_TOO_MANY,
                                  "more than %u kexts", KSYMS_MAX_KEXTS);
                ks->kext[ks->nkext++] = cur;
                memset(&cur, 0, sizeof cur);
                got_exec = got_size = false;
                in_kext  = has_pending = false;
            }
            p = t.after;
            continue;
        }

        /* ---- <key> ---------------------------------------------------- */
        if (name_is(&t, "key")) {
            if (t.selfclose)
                return plfail(&pl, p, KSYMS_ERR_PLIST, "self-closing <key/>");
            const char *tx = t.after;
            const char *lt = memchr(tx, '<', (size_t)(end - tx));
            if (!lt) return plfail(&pl, p, KSYMS_ERR_PLIST, "unterminated <key>");
            unsigned kl = (unsigned)(lt - tx);
            has_pending = false;
            if (in_kext && sp == root_sp + 1 && kl < sizeof pending) {
                memcpy(pending, tx, kl); pending[kl] = '\0';
                has_pending = true;
            }
            /* step over </key> */
            tag_t c;
            st = read_tag(&pl, lt, &c);
            if (st != KSYMS_OK) return st;
            if (!c.closing || !name_is(&c, "key"))
                return plfail(&pl, lt, KSYMS_ERR_PLIST, "<key> not closed by </key>");
            p = c.after;
            continue;
        }

        /* ---- containers ------------------------------------------------ */
        /* <plist version="1.0"> is a transparent wrapper: the shipped 3.x
         * kernelcache has none, but a hand-extracted plist will. */
        if (name_is(&t, "plist")) {
            if (!t.selfclose) {
                if (sp >= sizeof stack)
                    return plfail(&pl, p, KSYMS_ERR_PLIST, "nesting too deep");
                stack[sp++] = 'p';
            }
            p = t.after;
            continue;
        }
        if (name_is(&t, "dict") || name_is(&t, "array")) {
            bool is_dict = name_is(&t, "dict");
            if (t.selfclose) { has_pending = false; p = t.after; continue; }
            if (!is_dict && !root_seen) { root_seen = true; root_sp = sp + 1; }
            if (is_dict && root_seen && sp == root_sp) {
                in_kext = true; got_exec = got_size = false;
                memset(&cur, 0, sizeof cur);
            }
            if (sp >= sizeof stack)
                return plfail(&pl, p, KSYMS_ERR_PLIST, "nesting deeper than %u",
                              (unsigned)sizeof stack);
            stack[sp++] = is_dict ? 'd' : 'a';
            has_pending = false;
            p = t.after;
            continue;
        }

        /* ---- leaf values ------------------------------------------------ */
        bool textual = name_is(&t, "string") || name_is(&t, "integer") ||
                       name_is(&t, "data")   || name_is(&t, "real")    ||
                       name_is(&t, "date");
        bool boolean = name_is(&t, "true") || name_is(&t, "false");
        if (!textual && !boolean)
            return plfail(&pl, p, KSYMS_ERR_PLIST,
                          "unknown plist element <%.*s>", (int)t.namelen, t.name);

        const char *val = t.after;
        unsigned    vlen = 0;
        if (textual && !t.selfclose) {
            const char *lt = memchr(val, '<', (size_t)(end - val));
            if (!lt) return plfail(&pl, p, KSYMS_ERR_PLIST,
                                   "unterminated <%.*s>", (int)t.namelen, t.name);
            vlen = (unsigned)(lt - val);
            tag_t c;
            st = read_tag(&pl, lt, &c);
            if (st != KSYMS_OK) return st;
            if (!c.closing || c.namelen != t.namelen ||
                memcmp(c.name, t.name, t.namelen))
                return plfail(&pl, lt, KSYMS_ERR_PLIST,
                              "<%.*s> closed by </%.*s>", (int)t.namelen, t.name,
                              (int)c.namelen, c.name);
            p = c.after;
        } else {
            if (!t.selfclose && boolean) {
                /* <true></true>: step over the close tag. */
                const char *lt = memchr(val, '<', (size_t)(end - val));
                if (!lt) return plfail(&pl, p, KSYMS_ERR_PLIST, "unterminated bool");
                tag_t c;
                st = read_tag(&pl, lt, &c);
                if (st != KSYMS_OK) return st;
                p = c.after;
            } else {
                p = t.after;
            }
        }

        if (t.has_id && textual) {
            st = id_put(&pl, tag_at, t.id, val, vlen);
            if (st != KSYMS_OK) return st;
        }
        if (t.has_idref) {
            st = id_get(&pl, tag_at, t.idref, &val, &vlen);
            if (st != KSYMS_OK) return st;
        }

        if (!has_pending) continue;
        has_pending = false;

        if (!strcmp(pending, "CFBundleIdentifier")) {
            if (!name_is(&t, "string") && !t.has_idref)
                return plfail(&pl, tag_at, KSYMS_ERR_PLIST,
                              "CFBundleIdentifier is a <%.*s>, not a <string>",
                              (int)t.namelen, t.name);
            if (vlen >= KSYMS_BUNDLE_MAX)
                return plfail(&pl, tag_at, KSYMS_ERR_PLIST,
                              "CFBundleIdentifier is %u bytes, longer than the "
                              "%u we store", vlen, KSYMS_BUNDLE_MAX - 1);
            for (unsigned i = 0; i < vlen; i++) {
                char c = val[i];
                if (!is_namechar(c) && c != '.' && c != '+')
                    return plfail(&pl, tag_at, KSYMS_ERR_PLIST,
                                  "CFBundleIdentifier contains '%c' — either an "
                                  "XML entity we do not decode or not an "
                                  "identifier at all", c);
            }
            memcpy(cur.bundle, val, vlen); cur.bundle[vlen] = '\0';
        } else if (!strcmp(pending, "_PrelinkExecutable")) {
            st = parse_int(&pl, tag_at, val, vlen, &cur.addr);
            if (st != KSYMS_OK) return st;
            got_exec = true;
        } else if (!strcmp(pending, "_PrelinkExecutableSize")) {
            st = parse_int(&pl, tag_at, val, vlen, &cur.size);
            if (st != KSYMS_OK) return st;
            got_size = true;
        } else if (!strcmp(pending, "_PrelinkKmodInfo")) {
            st = parse_int(&pl, tag_at, val, vlen, &cur.kmod_info);
            if (st != KSYMS_OK) return st;
        } else if (!strcmp(pending, "_PrelinkExecutableLoadAddr")) {
            /*
             * The later (iOS 4+/OS X) spelling. If it ever turns up here the
             * assumption above is wrong and the map would silently be built
             * from the wrong key — so say so instead.
             */
            return plfail(&pl, tag_at, KSYMS_ERR_PLIST,
                          "this image uses _PrelinkExecutableLoadAddr, not "
                          "_PrelinkExecutable — teach ksyms.c the newer layout");
        }
    }

    if (sp)
        return plfail(&pl, end, KSYMS_ERR_PLIST,
                      "%u container(s) left open at the end of the plist", sp);
    if (!root_seen)
        return plfail(&pl, pl.base, KSYMS_ERR_PLIST,
                      "no root <array> of kext dicts");
    if (!ks->nkext_exec)
        return plfail(&pl, pl.base, KSYMS_ERR_PLIST,
                      "%u kext dicts but not one with a _PrelinkExecutable",
                      ks->nkext);

    qsort(ks->kext, ks->nkext, sizeof ks->kext[0], kext_cmp);

    /* Overlap is not fatal — the map is still usable — but it means one of the
     * two extents is wrong, so it must not pass silently. */
    for (unsigned i = 1; i < ks->nkext_exec; i++) {
        const kext_t *a = &ks->kext[i - 1], *b = &ks->kext[i];
        if ((uint64_t)a->addr + a->size > (uint64_t)b->addr) {
            /* Both identifiers are bounded in the parsed representation, but
             * two maximum-size names plus the explanatory prose exceed this
             * diagnostic buffer. Bound their presentation explicitly so the
             * warning remains NUL-terminated and warning-free under GCC's
             * -Wformat-truncation without weakening the parser's own limit. */
            snprintf(ks->detail, sizeof ks->detail,
                     "WARNING: %.63s@0x%08x+0x%x overlaps %.63s@0x%08x; "
                     "addresses in the overlap are attributed to the first",
                     a->bundle, a->addr, a->size, b->bundle, b->addr);
            break;
        }
    }
    return KSYMS_OK;
}

/* ===========================================================================
 * Public entry points.
 * ========================================================================= */

ksyms_status_t ksyms_load(ksyms_t *ks, const uint8_t *img, size_t len) {
    memset(ks, 0, sizeof *ks);
    ks->img = img; ks->len = len;

    macho_t m;
    macho_status_t mst = macho_parse(img, len, &m);
    if (mst != MACHO_OK) {
        ks->sym_status = ks->prelink_status = KSYMS_ERR_MACHO;
        snprintf(ks->detail, sizeof ks->detail, "macho_parse: %s",
                 macho_strerror(mst));
        return KSYMS_ERR_MACHO;
    }

    ks->sym_status     = symtab_load(ks, &m);
    ks->prelink_status = prelink_load(ks, &m);

    if (ks->sym_status != KSYMS_OK && ks->prelink_status != KSYMS_OK)
        return ks->sym_status;
    if (ks->prelink_status != KSYMS_OK) return ks->prelink_status;
    return ks->sym_status;
}

void ksyms_free(ksyms_t *ks) {
    free(ks->sym);
    ks->sym = NULL;
    ks->nsym = 0;
}

const kext_t *ksyms_kext_at(const ksyms_t *ks, uint32_t addr) {
    addr &= ~1u;
    if (!ks->nkext_exec) return NULL;
    unsigned lo = 0, hi = ks->nkext_exec;      /* last kext with addr <= a */
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (ks->kext[mid].addr <= addr) lo = mid + 1; else hi = mid;
    }
    if (!lo) return NULL;
    const kext_t *k = &ks->kext[lo - 1];
    return (uint64_t)addr < (uint64_t)k->addr + k->size ? k : NULL;
}

uint32_t ksyms_value(const ksyms_t *ks, const char *name) {
    for (unsigned i = 0; i < ks->nsym; i++)
        if (!strcmp(ks->sym[i].name, name)) return ks->sym[i].value;
    return 0;
}

const char *ksyms_resolve(const ksyms_t *ks, uint32_t addr,
                          char *buf, size_t bufsz) {
    addr &= ~1u;                                   /* drop the Thumb bit */

    const kext_t *k = ksyms_kext_at(ks, addr);
    if (k) {
        snprintf(buf, bufsz, "%s+0x%x", k->bundle, addr - k->addr);
        return buf;
    }
    if (ks->prelink_hi && addr >= ks->prelink_lo && addr < ks->prelink_hi) {
        /* Inside the prelink region but claimed by nobody: say exactly that
         * rather than falling through to the nearest kernel symbol, which
         * would be megabytes away and a lie. */
        snprintf(buf, bufsz, "__PRELINK_TEXT+0x%x", addr - ks->prelink_lo);
        return buf;
    }

    if (ks->nsym) {
        unsigned lo = 0, hi = ks->nsym;
        while (lo < hi) {
            unsigned mid = (lo + hi) / 2;
            if (ks->sym[mid].value <= addr) lo = mid + 1; else hi = mid;
        }
        if (lo) {
            const ksym_t *s = &ks->sym[lo - 1];
            uint32_t d = addr - s->value;
            if (d <= KSYMS_MAX_SYM_SPAN) {
                if (d) snprintf(buf, bufsz, "%s+0x%x", s->name, d);
                else   snprintf(buf, bufsz, "%s", s->name);
                return buf;
            }
        }
    }
    snprintf(buf, bufsz, "?");
    return buf;
}
