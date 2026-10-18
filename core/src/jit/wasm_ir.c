#include "wasm_ir.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void warena_init(warena *a, size_t cap) {
    a->mem = malloc(cap);
    a->cap = a->mem ? cap : 0;
    a->used = 0;
    a->oom = !a->mem;
}
void warena_reset(warena *a) { a->used = 0; a->oom = !a->mem; }
void warena_free(warena *a) { free(a->mem); memset(a, 0, sizeof *a); }

void *walloc(warena *a, size_t n) {
    static uint8_t scratch[256];
    n = (n + 15u) & ~(size_t)15u;
    if (a->oom || n > a->cap - a->used) {
        /* Callers keep building into scratch; the compile is discarded. */
        a->oom = true;
        memset(scratch, 0, sizeof scratch);
        return n <= sizeof scratch ? scratch : NULL;
    }
    void *p = a->mem + a->used;
    a->used += n;
    memset(p, 0, n);
    return p;
}

static wn *node(warena *a, uint8_t kind) {
    wn *n = walloc(a, sizeof *n);
    if (n) n->kind = kind;
    return n;
}

static void add_kid(warena *a, wn *n, wn *k) {
    if (!n || a->oom) return;
    if (n->nk == n->cap) {
        uint32_t cap = n->cap ? n->cap * 2u : 4u;
        if (cap > 255u) cap = 255u;
        if (n->nk == cap) { a->oom = true; return; }
        wn **k2 = walloc(a, cap * sizeof *k2);
        if (!k2 || a->oom) return;
        if (n->nk) memcpy(k2, n->k, n->nk * sizeof *k2);
        n->k = k2;
        n->cap = cap;
    }
    n->k[n->nk++] = k;
}

wn *w_op(warena *a, uint16_t op, int nk, ...) {
    wn *n = node(a, WN_OP);
    if (!n) return NULL;
    n->op = op;
    va_list ap;
    va_start(ap, nk);
    for (int i = 0; i < nk; i++) add_kid(a, n, va_arg(ap, wn *));
    va_end(ap);
    return n;
}

static wn *imm_op(warena *a, uint16_t op, uint8_t kind, int64_t v) {
    wn *n = w_op(a, op, 0);
    if (n) { n->imm_kind = kind; n->imm = v; }
    return n;
}
wn *w_i32(warena *a, int32_t v) { return imm_op(a, 0x41, WI_I32, v); }
wn *w_i64(warena *a, int64_t v) { return imm_op(a, 0x42, WI_I64, v); }
wn *w_get(warena *a, uint32_t l) { return imm_op(a, 0x20, WI_U32, l); }
wn *w_global_get(warena *a, uint32_t g) { return imm_op(a, 0x23, WI_U32, g); }
wn *w_set(warena *a, uint32_t l, wn *v) {
    wn *n = w_op(a, 0x21, 1, v);
    if (n) { n->imm_kind = WI_U32; n->imm = l; }
    return n;
}

static int natural_align(uint16_t op) {
    switch (op) {
    case 0x2c: case 0x2d: case 0x30: case 0x31: case 0x3a: case 0x3c: return 0;
    case 0x2e: case 0x2f: case 0x32: case 0x33: case 0x3b: case 0x3d: return 1;
    case 0x29: case 0x2b: case 0x37: case 0x39: return 3;
    default: return 2;
    }
}

wn *w_load(warena *a, uint16_t op, uint32_t offset, int align, wn *addr) {
    wn *n = w_op(a, op, 1, addr);
    if (n) {
        n->imm_kind = WI_MEM;
        n->imm = offset;
        n->imm2 = (uint32_t)(align < 0 ? natural_align(op) : align);
    }
    return n;
}
wn *w_store(warena *a, uint16_t op, uint32_t offset, int align, wn *addr, wn *v) {
    wn *n = w_op(a, op, 2, addr, v);
    if (n) {
        n->imm_kind = WI_MEM;
        n->imm = offset;
        n->imm2 = (uint32_t)(align < 0 ? natural_align(op) : align);
    }
    return n;
}
wn *w_call(warena *a, uint32_t func, int nk, ...) {
    wn *n = w_op(a, 0x10, 0);
    if (!n) return NULL;
    n->imm_kind = WI_U32;
    n->imm = func;
    va_list ap;
    va_start(ap, nk);
    for (int i = 0; i < nk; i++) add_kid(a, n, va_arg(ap, wn *));
    va_end(ap);
    return n;
}

wn *w_seq(warena *a) { return node(a, WN_SEQ); }
void w_push(warena *a, wn *seq, wn *item) { if (item) add_kid(a, seq, item); }
wlabel *w_label(warena *a) { return walloc(a, sizeof(wlabel)); }

wn *w_block(warena *a, wlabel *l, wn *body) {
    wn *n = node(a, WN_BLOCK);
    if (n) { n->label = l; n->then_ = body; }
    return n;
}
wn *w_loop(warena *a, wlabel *l, wn *body) {
    wn *n = node(a, WN_LOOP);
    if (n) { n->label = l; n->then_ = body; }
    return n;
}
wn *w_if(warena *a, wn *cond, wn *then_, wn *else_) {
    wn *n = node(a, WN_IF);
    if (n) { add_kid(a, n, cond); n->then_ = then_; n->else_ = else_; }
    return n;
}
wn *w_br(warena *a, wlabel *l) {
    wn *n = node(a, WN_BR);
    if (n) n->label = l;
    return n;
}
wn *w_br_if(warena *a, wlabel *l, wn *cond) {
    wn *n = node(a, WN_BR_IF);
    if (n) { n->label = l; add_kid(a, n, cond); }
    return n;
}
wn *w_br_table(warena *a, wlabel **t, uint32_t count, wlabel *def, wn *index) {
    wn *n = node(a, WN_BR_TABLE);
    if (!n) return NULL;
    n->table = walloc(a, count * sizeof *t);
    if (n->table && !a->oom) memcpy(n->table, t, count * sizeof *t);
    n->ntable = count;
    n->label = def;
    add_kid(a, n, index);
    return n;
}

void wbuf_byte(wbuf *b, uint8_t v) { wbuf_bytes(b, &v, 1); }
void wbuf_bytes(wbuf *b, const void *p, size_t n) {
    if (b->oom) return;
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096u;
        while (cap < b->len + n) cap *= 2u;
        uint8_t *nb = realloc(b->buf, cap);
        if (!nb) { b->oom = true; return; }
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, p, n);
    b->len += n;
}
void wbuf_u32(wbuf *b, uint32_t v) {
    do {
        uint8_t byte = v & 0x7fu;
        v >>= 7;
        wbuf_byte(b, (uint8_t)(byte | (v ? 0x80u : 0u)));
    } while (v);
}
void wbuf_s64(wbuf *b, int64_t v) {
    for (;;) {
        uint8_t byte = (uint8_t)(v & 0x7f);
        v >>= 7;
        bool done = (v == 0 && !(byte & 0x40)) || (v == -1 && (byte & 0x40));
        wbuf_byte(b, (uint8_t)(byte | (done ? 0u : 0x80u)));
        if (done) return;
    }
}
void wbuf_free(wbuf *b) { free(b->buf); memset(b, 0, sizeof *b); }

static void enc(wbuf *b, const wn *n, int *depth) {
    if (!n) return;
    switch (n->kind) {
    case WN_SEQ:
        for (unsigned i = 0; i < n->nk; i++) enc(b, n->k[i], depth);
        return;
    case WN_BLOCK: case WN_LOOP:
        wbuf_byte(b, n->kind == WN_BLOCK ? 0x02 : 0x03);
        wbuf_byte(b, 0x40);
        n->label->depth = (*depth)++;
        enc(b, n->then_, depth);
        wbuf_byte(b, 0x0b);
        (*depth)--;
        return;
    case WN_IF:
        enc(b, n->k[0], depth);
        wbuf_byte(b, 0x04);
        wbuf_byte(b, 0x40);
        (*depth)++;
        enc(b, n->then_, depth);
        if (n->else_) { wbuf_byte(b, 0x05); enc(b, n->else_, depth); }
        wbuf_byte(b, 0x0b);
        (*depth)--;
        return;
    case WN_BR:
        wbuf_byte(b, 0x0c);
        wbuf_u32(b, (uint32_t)(*depth - 1 - n->label->depth));
        return;
    case WN_BR_IF:
        enc(b, n->k[0], depth);
        wbuf_byte(b, 0x0d);
        wbuf_u32(b, (uint32_t)(*depth - 1 - n->label->depth));
        return;
    case WN_BR_TABLE:
        enc(b, n->k[0], depth);
        wbuf_byte(b, 0x0e);
        wbuf_u32(b, n->ntable);
        for (unsigned i = 0; i < n->ntable; i++)
            wbuf_u32(b, (uint32_t)(*depth - 1 - n->table[i]->depth));
        wbuf_u32(b, (uint32_t)(*depth - 1 - n->label->depth));
        return;
    default:
        break;
    }
    for (unsigned i = 0; i < n->nk; i++) enc(b, n->k[i], depth);
    if (n->op >= 0xfc00u) { wbuf_byte(b, 0xfc); wbuf_u32(b, n->op & 0xffu); }
    else wbuf_byte(b, (uint8_t)n->op);
    switch (n->imm_kind) {
    case WI_I32: case WI_I64: wbuf_s64(b, n->imm); break;
    case WI_U32: wbuf_u32(b, (uint32_t)n->imm); break;
    case WI_MEM: wbuf_u32(b, n->imm2); wbuf_u32(b, (uint32_t)n->imm); break;
    case WI_F32Z: { static const uint8_t z[4]; wbuf_bytes(b, z, 4); break; }
    case WI_F64Z: { static const uint8_t z[8]; wbuf_bytes(b, z, 8); break; }
    case WI_CALLI: wbuf_u32(b, (uint32_t)n->imm); wbuf_u32(b, n->imm2); break;
    default: break;
    }
}

void w_encode(wbuf *b, const wn *n) {
    int depth = 0;
    enc(b, n, &depth);
}
