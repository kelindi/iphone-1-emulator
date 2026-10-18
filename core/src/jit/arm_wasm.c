/* ARMv6/Thumb guarded 1 KiB region compiler, emitting WebAssembly binary.
 * A C port of tools/jit/compiler.cjs; the two must produce identical seeds.
 * Not reentrant: one compile at a time per process. */
#include "arm_wasm.h"
#include "wasm_ir.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum {
    P_C, P_BUDGET, P_N,
    L_R15, L_A, L_B, L_R, L_F, L_P, L_ENTRY, L_ADDR, L_WHY,
    L_WIDE, L_RH, L_WH,
    L_PRIV, L_POFS, L_FN, L_FZ, L_FC, L_FV, L_FS, L_E,
    L_SA, L_SB, L_SZ, L_DA, L_DB, L_DZ, L_UA, L_UB, L_V0
};
#define DREAD_ENTRIES 1024

static warena *A;
static uint32_t OFF[16], R, F, FETCH;
static wlabel *DONE;
/* Per-op emission facts that the JS compiler recovered with regexes. */
static struct { uint32_t t[64]; unsigned n; bool writes_f, on; } ST;

#define C(v) w_i32(A, (int32_t)(uint32_t)(v))
#define L(x) w_get(A, (x))
#define OP1(o, a) w_op(A, (o), 1, (a))
#define OP2(o, a, b) w_op(A, (o), 2, (a), (b))
#define OP3(o, a, b, c) w_op(A, (o), 3, (a), (b), (c))
#define ADD(a, b) OP2(0x6a, a, b)
#define SUB(a, b) OP2(0x6b, a, b)
#define AND(a, b) OP2(0x71, a, b)
#define OR(a, b) OP2(0x72, a, b)
#define XOR(a, b) OP2(0x73, a, b)
#define SHL(a, n) OP2(0x74, a, C(n))
#define SHR(a, n) OP2(0x76, a, C(n))
#define EQZ(a) OP1(0x45, a)
#define EQ(a, b) OP2(0x46, a, b)
#define NE(a, b) OP2(0x47, a, b)
#define SEQ() w_seq(A)

static wn *seq_of(int n, ...) {
    wn *s = SEQ();
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) w_push(A, s, va_arg(ap, wn *));
    va_end(ap);
    return s;
}
#define CAT(s, x) w_push(A, (s), (x))

static wn *set(unsigned l, wn *v) {
    if (ST.on && l == L_F) ST.writes_f = true;
    return w_set(A, l, v);
}
static wn *ld32(uint32_t off) { return w_load(A, 0x28, off, -1, L(P_C)); }
static wn *reg(unsigned n) { return n == 15 ? L(L_R15) : ld32(R + n * 4u); }
static wn *put(unsigned n, wn *x) {
    if (n == 15) {
        if (ST.on && x && x->kind == WN_OP && x->op == 0x41 && ST.n < 64)
            ST.t[ST.n++] = (uint32_t)x->imm;
        return w_set(A, L_R15, x);
    }
    return w_store(A, 0x36, R + n * 4u, -1, L(P_C), x);
}
static wn *FE(void) { return ld32(FETCH); }
static wn *TG(void) { return ld32(OFF[6]); }
static wn *HW(void) { return w_load(A, 0x28, OFF[10], -1, ld32(OFF[9])); }
static wn *fail(int why) { return seq_of(2, w_set(A, L_WHY, C(why)), w_br(A, DONE)); }
static wn *if_then(wn *c, wn *t) { return w_if(A, c, t, NULL); }

static wn *carry(void) { return L(L_FC); }
static wn *packed(void) {
    return OR(OR(AND(L(L_F), C(0x0fffffff)), AND(L(L_FN), C(0x80000000u))),
              OR(OR(SHL(EQZ(L(L_FZ)), 30), SHL(L(L_FC), 29)), SHL(SHR(L(L_FV), 31), 28)));
}
static wn *unpack(void) {
    return seq_of(4, set(L_FN, L(L_F)), set(L_FZ, EQZ(AND(L(L_F), C(0x40000000)))),
                  set(L_FC, AND(SHR(L(L_F), 29), C(1))), set(L_FV, SHL(L(L_F), 3)));
}
static wn *flags(bool logical, wn *cy) {
    wn *s = seq_of(3, set(L_FC, cy), set(L_FN, L(L_R)), set(L_FZ, L(L_R)));
    if (!logical)
        CAT(s, set(L_FV, AND(XOR(XOR(L(L_A), L(L_B)), C(-1)), XOR(L(L_A), L(L_R)))));
    return s;
}
static wn *ext(wn *x) { return OP1(0xad, x); }
static wn *arith(wn *a, wn *b, wn *cin, bool save) {
    wn *s = seq_of(2, set(L_A, a), set(L_B, b));
    if (!save) { CAT(s, set(L_R, ADD(ADD(L(L_A), L(L_B)), cin))); return s; }
    CAT(s, set(L_WIDE, OP2(0x7c, OP2(0x7c, ext(L(L_A)), ext(L(L_B))), ext(cin))));
    CAT(s, set(L_R, OP1(0xa7, L(L_WIDE))));
    CAT(s, flags(false, OP1(0xa7, OP2(0x88, L(L_WIDE), w_i64(A, 32)))));
    return s;
}
static wn *alu(unsigned code, wn *a, wn *b, bool S, unsigned rd, wn *cy) {
    wn *s;
    bool write = !(code >= 8 && code <= 11);
    if (!cy) cy = carry();
    if (code == 2 || code == 6 || code == 10)
        s = arith(a, XOR(b, C(-1)), code == 6 ? carry() : C(1), S || code == 10);
    else if (code == 3 || code == 7)
        s = arith(b, XOR(a, C(-1)), code == 7 ? carry() : C(1), S);
    else if (code == 4 || code == 5 || code == 11)
        s = arith(a, b, code == 5 ? carry() : C(0), S || code == 11);
    else {
        wn *x = code == 0 || code == 8 ? AND(a, b) : code == 1 || code == 9 ? XOR(a, b)
              : code == 12 ? OR(a, b) : code == 13 ? b : code == 14 ? AND(a, XOR(b, C(-1)))
              : XOR(b, C(-1));
        s = seq_of(1, set(L_R, x));
        if (S || !write) CAT(s, flags(true, cy));
    }
    if (write) CAT(s, put(rd, L(L_R)));
    return s;
}
static wn *condition(unsigned code) {
    wn *base;
    switch (code >> 1) {
    case 0: base = EQZ(L(L_FZ)); break;
    case 1: base = L(L_FC); break;
    case 2: base = OP2(0x48, L(L_FN), C(0)); break;
    case 3: base = OP2(0x48, L(L_FV), C(0)); break;
    case 4: base = AND(L(L_FC), NE(L(L_FZ), C(0))); break;
    case 5: base = OP2(0x4e, XOR(L(L_FN), L(L_FV)), C(0)); break;
    case 6: base = AND(OP2(0x4e, XOR(L(L_FN), L(L_FV)), C(0)), NE(L(L_FZ), C(0))); break;
    default: base = C(1); break;
    }
    return code & 1 ? EQZ(base) : base;
}
static wn *span(wn *addr, unsigned bytes, bool load, unsigned transactions) {
    wn *s = seq_of(1, set(L_ADDR, addr));
    if (bytes > 1) CAT(s, if_then(AND(L(L_ADDR), C(bytes >= 4 ? 3 : 1)), fail(6)));
    if (bytes > 4)
        CAT(s, if_then(OP2(0x4b, ADD(AND(L(L_ADDR), C(1023)), C(bytes)), C(1024)), fail(6)));
    wn *slot = AND(ADD(SHR(L(L_ADDR), 10), L(L_POFS)), C(DREAD_ENTRIES - 1));
    CAT(s, set(L_ENTRY, ADD(ADD(L(P_C), C(OFF[load ? 4 : 5])), OP2(0x6c, slot, C(OFF[11])))));
    CAT(s, set(L_P, w_load(A, 0x28, 0, -1, L(L_ENTRY))));
    wn *want = OP2(0x84, ext(OR(AND(L(L_ADDR), C(~1023u)), L(L_PRIV))),
                   OP2(0x86, ext(TG()), w_i64(A, 32)));
    CAT(s, if_then(OP2(0x52, w_load(A, 0x29, 4, 2, L(L_ENTRY)), want), fail(6)));
    if (!load) CAT(s, if_then(EQZ(HW()), fail(6)));
    CAT(s, set(L_P, ADD(L(L_P), AND(L(L_ADDR), C(1023)))));
    if (!load)
        CAT(s, if_then(AND(OP2(0x49, L(L_P), ADD(FE(), C(1024))),
                           OP2(0x4b, ADD(L(L_P), C(bytes)), FE())), fail(7)));
    unsigned h = load ? L_RH : L_WH;
    CAT(s, set(h, OP2(0x7c, L(h), w_i64(A, transactions))));
    return s;
}
static wn *transfer(wn *addr, unsigned bytes, bool load, unsigned rd) {
    wn *s = span(addr, bytes, load, 1);
    if (load) CAT(s, put(rd, w_load(A, bytes == 4 ? 0x28 : bytes == 2 ? 0x2f : 0x2d, 0, -1, L(L_P))));
    else CAT(s, w_store(A, bytes == 4 ? 0x36 : bytes == 2 ? 0x3b : 0x3a, 0, -1, L(L_P), reg(rd)));
    return s;
}
static wn *badpc(unsigned t) {
    wn *then_ = SEQ();
    if (t) CAT(then_, set(L_RH, OP2(0x7d, L(L_RH), w_i64(A, t))));
    CAT(then_, w_br(A, DONE));
    return if_then(EQ(AND(L(L_R), C(3)), C(2)), then_);
}
static wn *interwork(void) { return set(L_F, OR(AND(L(L_F), C(~32u)), SHL(AND(L(L_R), C(1)), 5))); }
static wn *shift(wn *v, unsigned type, unsigned amount, wn **cy) {
    wn *x;
    *cy = carry();
    if (type == 0) {
        x = amount ? SHL(v, amount) : v;
        if (amount) *cy = AND(SHR(v, 32 - amount), C(1));
    } else if (type == 1) {
        x = amount ? SHR(v, amount) : C(0);
        *cy = AND(SHR(v, amount ? amount - 1 : 31), C(1));
    } else if (type == 2) {
        x = OP2(0x75, v, C(amount ? amount : 31));
        *cy = AND(SHR(v, amount ? amount - 1 : 31), C(1));
    } else if (amount) {
        x = OP2(0x78, v, C(amount));
        *cy = AND(SHR(v, amount - 1), C(1));
    } else {
        x = OR(SHL(carry(), 31), SHR(v, 1));
        *cy = AND(v, C(1));
    }
    return x;
}
static int32_t sx(uint32_t v, unsigned left, unsigned right) {
    return (int32_t)(v << left) >> right;
}
static wn *pc_of_r(void) { return AND(L(L_R), C(~1u)); }

typedef struct { wn *body; bool stop; } op_t;

static bool emit_base(uint32_t insn, uint32_t pc, bool thumb, op_t *o) {
    wn *body = SEQ(), *next = C(pc + (thumb ? 2u : 4u));
    bool stop = false;
#define RR(r) ((r) == 15 ? C(pc + (thumb ? 4u : 8u)) : reg(r))
    if (!thumb) {
        unsigned cond = insn >> 28;
        if (cond == 15) return false;
        unsigned rn = insn >> 16 & 15, rd = insn >> 12 & 15, code = insn >> 21 & 15;
        bool S = insn & 0x100000;
        if ((insn & 0x0e000000) == 0x0a000000) {
            next = C(pc + 8u + (uint32_t)sx(insn, 8, 6));
            if (insn & 0x1000000) CAT(body, put(14, C(pc + 4)));
            stop = true;
        } else if ((insn & 0x0ffffff0) == 0x012fff10) {
            CAT(body, set(L_R, RR(insn & 15)));
            CAT(body, if_then(EQ(AND(L(L_R), C(3)), C(2)), w_br(A, DONE)));
            CAT(body, interwork());
            next = pc_of_r();
            stop = true;
        } else if ((insn & 0x0f700000) == 0x05100000 && rd == 15) {
            wn *addr = (insn & 0x800000) ? ADD(RR(rn), C(insn & 4095)) : SUB(RR(rn), C(insn & 4095));
            CAT(body, span(addr, 4, true, 1));
            CAT(body, set(L_R, w_load(A, 0x28, 0, -1, L(L_P))));
            CAT(body, badpc(1));
            CAT(body, interwork());
            next = pc_of_r();
            stop = true;
        } else if ((insn & 0x0f200000) == 0x05000000 && rd != 15) {
            wn *addr = (insn & 0x800000) ? ADD(RR(rn), C(insn & 4095)) : SUB(RR(rn), C(insn & 4095));
            CAT(body, transfer(addr, (insn & 0x400000) ? 1 : 4, insn & 0x100000, rd));
        } else if ((insn & 0x0e400000) == 0x08000000 && rn != 15 && (insn & 0xffff) &&
                   (!(insn & 0x8000) || (insn & 0x100000)) &&
                   !((insn & 0x200000) && (insn & (1u << rn)))) {
            bool withPc = insn & 0x8000, load = insn & 0x100000, U = insn & 0x800000,
                 P = insn & 0x1000000;
            unsigned regs[15], nr = 0;
            for (unsigned i = 0; i < 15; i++) if (insn & (1u << i)) regs[nr++] = i;
            unsigned count = nr + (withPc ? 1 : 0);
            CAT(body, set(L_A, reg(rn)));
            int32_t d = U ? (P ? 4 : 0) : (P ? -4 * (int32_t)count : -4 * (int32_t)count + 4);
            CAT(body, span(ADD(L(L_A), C(d)), count * 4, load, count));
            if (withPc) {
                CAT(body, set(L_R, w_load(A, 0x28, nr * 4, -1, L(L_P))));
                CAT(body, badpc(count));
            }
            for (unsigned i = 0; i < nr; i++)
                CAT(body, load ? put(regs[i], w_load(A, 0x28, i * 4, -1, L(L_P)))
                               : w_store(A, 0x36, i * 4, -1, L(L_P), reg(regs[i])));
            if (insn & 0x200000)
                CAT(body, put(rn, ADD(L(L_A), C(U ? 4 * (int32_t)count : -4 * (int32_t)count))));
            if (withPc) { CAT(body, interwork()); next = pc_of_r(); stop = true; }
        } else if ((insn & 0x0c000000) == 0 && (insn & 0x01900000) != 0x01000000 &&
                   !((insn & 0x0e000000) == 0 && (insn & 0x10)) && rd != 15) {
            wn *b, *cy = carry();
            if (insn & 0x2000000) {
                unsigned rot = (insn >> 8 & 15) * 2;
                uint32_t imm = insn & 255;
                if (rot) imm = (imm >> rot) | (imm << (32 - rot));
                b = C(imm);
                if (rot) cy = C(imm >> 31);
            } else {
                b = shift(RR(insn & 15), insn >> 5 & 3, insn >> 7 & 31, &cy);
            }
            CAT(body, alu(code, RR(rn), b, S, rd, cy));
        } else return false;
        if (cond != 14) {
            wn *t = seq_of(1, body);
            CAT(t, put(15, next));
            body = seq_of(1, w_if(A, condition(cond), t, put(15, C(pc + 4))));
        } else CAT(body, put(15, next));
    } else {
        unsigned group = insn >> 12, rd = insn & 7, rs = insn >> 3 & 7;
        if (insn < 0x1800) {
            unsigned type = insn >> 11 & 3, amount = insn >> 6 & 31;
            wn *b, *cy = carry();
            if (type == 0) {
                b = amount ? SHL(reg(rs), amount) : reg(rs);
                if (amount) cy = AND(SHR(reg(rs), 32 - amount), C(1));
            } else if (type == 1) {
                b = amount ? SHR(reg(rs), amount) : C(0);
                cy = AND(SHR(reg(rs), amount ? amount - 1 : 31), C(1));
            } else {
                b = OP2(0x75, reg(rs), C(amount ? amount : 31));
                cy = AND(SHR(reg(rs), amount ? amount - 1 : 31), C(1));
            }
            CAT(body, alu(13, C(0), b, true, rd, cy));
        } else if (insn < 0x2000) {
            wn *b = (insn & 0x400) ? C(insn >> 6 & 7) : reg(insn >> 6 & 7);
            CAT(body, alu((insn & 0x200) ? 2 : 4, reg(rs), b, true, rd, NULL));
        } else if (insn < 0x4000) {
            static const unsigned codes[4] = {13, 10, 4, 2};
            unsigned target = insn >> 8 & 7;
            CAT(body, alu(codes[insn >> 11 & 3], reg(target), C(insn & 255), true, target, NULL));
        } else if ((insn & 0xfc00) == 0x4000) {
            unsigned code = insn >> 6 & 15;
            if (code == 9) CAT(body, alu(2, C(0), reg(rs), true, rd, NULL));
            else if (code == 13)
                CAT(body, seq_of(4, set(L_R, OP2(0x6c, reg(rd), reg(rs))), set(L_FN, L(L_R)),
                                 set(L_FZ, L(L_R)), put(rd, L(L_R))));
            else if ((0xdd63u >> code) & 1u) CAT(body, alu(code, reg(rd), reg(rs), true, rd, NULL));
            else return false;
        } else if ((insn & 0xfc00) == 0x4400) {
            unsigned target = rd | (insn >> 4 & 8), source = rs | (insn >> 3 & 8), code = insn >> 8 & 3;
            if (code == 3) {
                if (source == 15 || (insn & 7)) return false;
                CAT(body, set(L_R, reg(source)));
                CAT(body, badpc(0));
                if (insn & 0x80) CAT(body, put(14, C((pc + 2) | 1)));
                CAT(body, interwork());
                next = pc_of_r();
                stop = true;
            } else {
                if (target == 15) return false;
                CAT(body, alu(code == 0 ? 4 : code == 1 ? 10 : 13, RR(target), RR(source), code == 1, target, NULL));
            }
        } else if ((insn & 0xf800) == 0x4800) {
            CAT(body, transfer(C(((pc + 4) & ~3u) + ((insn & 255) << 2)), 4, true, insn >> 8 & 7));
        } else if (group >= 6 && group <= 9) {
            unsigned bytes = group == 8 ? 2 : group == 7 ? 1 : 4;
            unsigned target = group == 9 ? insn >> 8 & 7 : rd;
            wn *addr = group == 9 ? ADD(reg(13), C((insn & 255) * 4)) : ADD(reg(rs), C((insn >> 6 & 31) * bytes));
            CAT(body, transfer(addr, bytes, insn & 0x800, target));
        } else if (group == 10) {
            CAT(body, put(insn >> 8 & 7, ADD((insn & 0x800) ? reg(13) : C((pc + 4) & ~3u), C((insn & 255) * 4))));
        } else if ((insn & 0xff00) == 0xb000) {
            CAT(body, put(13, ADD(reg(13), C((int32_t)(insn & 127) * ((insn & 128) ? -4 : 4)))));
        } else if ((insn & 0xf600) == 0xb400) {
            bool load = insn & 0x800, extra = insn & 256, popPc = load && extra;
            unsigned regs[9], nr = 0;
            for (unsigned i = 0; i < 8; i++) if (insn & (1u << i)) regs[nr++] = i;
            if (extra && !load) regs[nr++] = 14;
            unsigned count = nr + (popPc ? 1 : 0);
            if (!count) return false;
            CAT(body, set(L_A, reg(13)));
            CAT(body, span(load ? L(L_A) : ADD(L(L_A), C(-4 * (int32_t)count)), 4 * count, load, count));
            if (popPc) {
                CAT(body, set(L_R, w_load(A, 0x28, nr * 4, -1, L(L_P))));
                CAT(body, badpc(count));
            }
            for (unsigned i = 0; i < nr; i++)
                CAT(body, load ? put(regs[i], w_load(A, 0x28, i * 4, -1, L(L_P)))
                               : w_store(A, 0x36, i * 4, -1, L(L_P), reg(regs[i])));
            CAT(body, put(13, ADD(L(L_A), C((load ? 4 : -4) * (int32_t)count))));
            if (popPc) { CAT(body, interwork()); next = pc_of_r(); stop = true; }
        } else if (group == 13 && (insn >> 8 & 15) < 14) {
            wn *c = condition(insn >> 8 & 15);
            wn *t = put(15, C(pc + 4 + (uint32_t)sx(insn, 24, 23)));
            o->body = seq_of(1, w_if(A, c, t, put(15, C(pc + 2))));
            o->stop = true;
            return true;
        } else if ((insn & 0xf800) == 0xe000) {
            next = C(pc + 4 + (uint32_t)sx(insn, 21, 20));
            stop = true;
        } else if (group == 15) {
            if (!(insn & 0x800)) CAT(body, put(14, C(pc + 4 + (uint32_t)sx(insn, 21, 9))));
            else {
                CAT(body, set(L_A, ADD(reg(14), C((insn & 2047) * 2))));
                CAT(body, put(14, C((pc + 2) | 1)));
                next = AND(L(L_A), C(~1u));
                stop = true;
            }
        } else return false;
        CAT(body, put(15, next));
    }
#undef RR
    o->body = body;
    o->stop = stop;
    return true;
}
static wn *emit_vfp(uint32_t insn);
static wn *vfp_allowed(void) {
    wn *cp10 = AND(SHR(ld32(OFF[14]), 20), C(3)), *cp11 = AND(SHR(ld32(OFF[14]), 22), C(3));
    wn *acc = OP3(0x1b, cp10, cp11, OP2(0x49, cp10, cp11));
    return OR(EQ(acc, C(3)), AND(EQ(acc, C(1)), NE(AND(L(L_F), C(31)), C(16))));
}
static bool is_vfp(uint32_t insn) {
    unsigned cp = insn >> 8 & 15;
    return (cp == 10 || cp == 11) &&
           ((insn & 0x0f000e00) == 0x0e000a00 || (insn & 0x0fe00e00) == 0x0c400a00);
}
static uint16_t ld_op(unsigned bytes, bool sign) {
    return bytes == 4 ? 0x28 : bytes == 2 ? (sign ? 0x2e : 0x2f) : (sign ? 0x2c : 0x2d);
}
static uint16_t st_op(unsigned bytes) { return bytes == 4 ? 0x36 : bytes == 2 ? 0x3b : 0x3a; }

static bool emit(uint32_t insn, uint32_t pc, bool thumb, op_t *o) {
    wn *body = SEQ(), *next = C(pc + (thumb ? 2u : 4u));
    if (!thumb) {
        unsigned cond = insn >> 28, rn = insn >> 16 & 15, rd = insn >> 12 & 15, rm = insn & 15;
#define RR(r) ((r) == 15 ? C(pc + 8u) : reg(r))
        if (cond == 15) return emit_base(insn, pc, thumb, o);
        if (is_vfp(insn)) {
            wn *v = emit_vfp(insn);
            if (!v) return false;
            CAT(body, v);
        } else if ((insn & 0x0f200e00) == 0x0d000a00) {
            bool D = insn & 0x400000, dbl = insn & 0x100, load = insn & 0x100000, U = insn & 0x800000;
            unsigned first = rd * 2 + (dbl ? 0 : D);
            if (dbl && D) return false;
            CAT(body, if_then(OR(EQZ(vfp_allowed()),
                                 OR(EQZ(AND(ld32(OFF[13]), C(1u << 30))),
                                    NE(AND(ld32(OFF[15]), C(0xc00000)), C(0)))),
                              w_br(A, DONE)));
            wn *addr = U ? ADD(RR(rn), C((insn & 255) * 4)) : SUB(RR(rn), C((insn & 255) * 4));
            uint32_t offset = OFF[12] + first * 4;
            CAT(body, span(addr, dbl ? 8 : 4, load, dbl ? 2 : 1));
            uint16_t l = dbl ? 0x29 : 0x28, s = dbl ? 0x37 : 0x36;
            CAT(body, load ? w_store(A, s, offset, -1, L(P_C), w_load(A, l, 0, -1, L(L_P)))
                           : w_store(A, s, 0, -1, L(L_P), w_load(A, l, offset, -1, L(P_C))));
        } else if ((insn & 0x0fc000f0) == 0x00000090) {
            unsigned dest = insn >> 16 & 15, acc = insn >> 12 & 15, rs = insn >> 8 & 15;
            bool Ac = insn & 0x200000, S = insn & 0x100000;
            if (dest == 15 || rs == 15 || rm == 15 || (Ac && acc == 15)) return false;
            wn *v = OP2(0x6c, reg(rm), reg(rs));
            if (Ac) v = ADD(v, reg(acc));
            CAT(body, set(L_R, v));
            CAT(body, put(dest, L(L_R)));
            if (S) CAT(body, set(L_F, OR(AND(L(L_F), C(0x3fffffff)),
                                         OR(AND(L(L_R), C(0x80000000u)), SHL(EQZ(L(L_R)), 30)))));
        } else if ((insn & 0x0e000090) == 0x00000090 && (insn & 0x60)) {
            bool P = insn & 0x1000000, U = insn & 0x800000, I = insn & 0x400000, W = insn & 0x200000,
                 load = insn & 0x100000, wb = !P || W;
            unsigned sh = insn >> 5 & 3;
            if (rd == 15 || (!P && W) || (wb && (rn == 15 || rn == rd)) ||
                (!I && ((insn & 0xf00) || rm == 15)) || (!load && sh != 1)) return false;
            wn *off = I ? C(((insn >> 8 & 15) << 4) | (insn & 15)) : RR(rm);
            unsigned bytes = sh == 2 ? 1 : 2;
            CAT(body, set(L_A, U ? ADD(RR(rn), off) : SUB(RR(rn), off)));
            CAT(body, span(P ? L(L_A) : RR(rn), bytes, load, 1));
            CAT(body, load ? put(rd, w_load(A, sh == 1 ? 0x2f : sh == 2 ? 0x2c : 0x2e, 0, -1, L(L_P)))
                           : w_store(A, 0x3b, 0, -1, L(L_P), reg(rd)));
            if (wb) CAT(body, put(rn, L(L_A)));
        } else if ((insn & 0x0c000000) == 0x04000000 && rd != 15 &&
                   (!(insn & 0x02000000) || !(insn & 16))) {
            bool I = insn & 0x2000000, P = insn & 0x1000000, U = insn & 0x800000, B = insn & 0x400000,
                 W = insn & 0x200000, load = insn & 0x100000, wb = !P || W;
            if ((!P && W) || (wb && (rn == 15 || rn == rd)) || (I && rm == 15)) return false;
            wn *cy, *off = I ? shift(RR(rm), insn >> 5 & 3, insn >> 7 & 31, &cy) : C(insn & 4095);
            CAT(body, set(L_A, U ? ADD(RR(rn), off) : SUB(RR(rn), off)));
            CAT(body, transfer(P ? L(L_A) : RR(rn), B ? 1 : 4, load, rd));
            if (wb) CAT(body, put(rn, L(L_A)));
        } else if ((insn & 0x0f8003f0) == 0x06800070 && rd != 15 && rm != 15 &&
                   ((1u << (insn >> 20 & 15)) & 0xdc00u) &&
                   !((insn >> 20 & 15) == 0xc && rn != 15)) {
            unsigned kind = insn >> 20 & 15, rot = (insn >> 10 & 3) * 8;
            wn *v = rot ? OP2(0x78, reg(rm), C(rot)) : reg(rm);
            wn *x = kind == 0xa ? OP1(0xc0, v) : kind == 0xb ? OP1(0xc1, v)
                  : AND(v, C(kind == 0xc ? 0x00ff00ff : kind == 0xe ? 0xff : 0xffff));
            if (rn != 15) x = ADD(reg(rn), x);
            CAT(body, put(rd, x));
        } else if ((insn & 0x0fff0fff) == 0x010f0000 && rd != 15) {
            CAT(body, put(rd, packed()));
        } else if ((insn & 0x0fff0fff) == 0x0e070f5a) {
        } else return emit_base(insn, pc, thumb, o);
#undef RR
        if (cond == 14) CAT(body, put(15, next));
        else {
            wn *t = seq_of(1, body);
            CAT(t, put(15, next));
            body = seq_of(1, w_if(A, condition(cond), t, put(15, C(pc + 4))));
        }
        o->body = body;
        o->stop = false;
        return true;
    }
    if ((insn & 0xf800) == 0xe800) {
        CAT(body, set(L_A, ADD(reg(14), C((insn & 2047) * 2))));
        CAT(body, put(14, C((pc + 2) | 1)));
        CAT(body, set(L_F, AND(L(L_F), C(~32u))));
        CAT(body, put(15, AND(L(L_A), C(~3u))));
        o->body = body;
        o->stop = true;
        return true;
    }
    if ((insn & 0xff00) == 0xb200) {
        unsigned kind = insn >> 6 & 3;
        wn *v = reg(insn >> 3 & 7);
        CAT(body, put(insn & 7, kind == 0 ? OP1(0xc1, v) : kind == 1 ? OP1(0xc0, v)
                                : AND(v, C(kind == 2 ? 0xffff : 0xff))));
    } else if ((insn & 0xf000) == 0x5000) {
        static const unsigned sizes[8] = {4, 2, 1, 1, 4, 2, 1, 2};
        unsigned code = insn >> 9 & 7, rd = insn & 7, bytes = sizes[code];
        bool load = code >= 3;
        CAT(body, span(ADD(reg(insn >> 3 & 7), reg(insn >> 6 & 7)), bytes, load, 1));
        CAT(body, load ? put(rd, w_load(A, ld_op(bytes, code == 7 || code == 3), 0, -1, L(L_P)))
                       : w_store(A, st_op(bytes), 0, -1, L(L_P), reg(rd)));
    } else return emit_base(insn, pc, thumb, o);
    CAT(body, put(15, next));
    o->body = body;
    o->stop = false;
    return true;
}
/* VFP11 subset, bit-exact with core/src/arm/vfp.c as built for the browser. */
#define SV(k) (OFF[12] + 4u * (k))
static wn *f0(bool dbl) { wn *n = w_op(A, dbl ? 0x44 : 0x43, 0); if (n) n->imm_kind = dbl ? WI_F64Z : WI_F32Z; return n; }
static wn *ldS(unsigned k) { return w_load(A, 0x2a, SV(k), -1, L(P_C)); }
static wn *ldD(unsigned k) { return w_load(A, 0x2b, SV(2 * k), -1, L(P_C)); }
static wn *stF(bool dbl, unsigned k, wn *x) { return w_store(A, dbl ? 0x39 : 0x38, SV(dbl ? 2 * k : k), -1, L(P_C), x); }
static wn *vfp_gate(void) {
    wn *s = seq_of(2, set(L_FS, ld32(OFF[15])), set(L_E, C(0)));
    CAT(s, if_then(OR(EQZ(vfp_allowed()), OR(EQZ(AND(ld32(OFF[13]), C(1u << 30))),
                                             AND(L(L_FS), C(0x00f79f00)))), w_br(A, DONE)));
    return s;
}
enum { IDC = 0x80, UFC = 0x8, IOC = 0x1 };
static wn *flush(bool dbl, unsigned v, unsigned bit) {
    wn *tiny = dbl
        ? OP2(0x54, OP2(0x7d, OP2(0x83, OP1(0xbd, L(v)), w_i64(A, INT64_C(0x7fffffffffffffff))), w_i64(A, 1)),
              w_i64(A, INT64_C(0x000fffffffffffff)))
        : OP2(0x49, SUB(AND(OP1(0xbc, L(v)), C(0x7fffffff)), C(1)), C(0x7fffff));
    wn *t = seq_of(2, set(L_E, OR(L(L_E), C(bit))), set(v, OP2(dbl ? 0xa6 : 0x98, f0(dbl), L(v))));
    return if_then(AND(L(L_FS), C(0x1000000)), if_then(tiny, t));
}
static wn *nan_exit(bool dbl, unsigned v) { return if_then(OP2(dbl ? 0x62 : 0x5c, L(v), L(v)), w_br(A, DONE)); }
static wn *commit(void) { return if_then(L(L_E), w_store(A, 0x36, OFF[15], -1, L(P_C), OR(L(L_FS), L(L_E)))); }
/* o: 0 add, 1 sub, 2 mul, 3 div */
static wn *fop(bool dbl, unsigned o, wn *a, wn *b) {
    static const uint16_t ops32[4] = {0x92, 0x93, 0x94, 0x95}, ops64[4] = {0xa0, 0xa1, 0xa2, 0xa3};
    unsigned VA = dbl ? L_DA : L_SA, VB = dbl ? L_DB : L_SB, Z = dbl ? L_DZ : L_SZ;
    return seq_of(7, set(VA, a), set(VB, b), flush(dbl, VA, IDC), flush(dbl, VB, IDC),
                  set(Z, OP2(dbl ? ops64[o] : ops32[o], L(VA), L(VB))), nan_exit(dbl, Z), flush(dbl, Z, UFC));
}
static wn *fneg(bool dbl, wn *x) { return OP1(dbl ? 0x9a : 0x8c, x); }
static wn *emit_vfp(uint32_t insn) {
#define BIT(i) ((insn >> (i)) & 1u)
    unsigned vn = insn >> 16 & 15, vd = insn >> 12 & 15, vm = insn & 15;
    bool dbl = BIT(8);
    if ((insn & 0x0fe00e00) == 0x0c400a00) {
        unsigned rt = vd, rt2 = vn, Ld = BIT(20), lo, hi;
        if ((insn & 0xc0) || rt == 15 || rt2 == 15) return NULL;
        if (dbl) { if (BIT(5)) return NULL; lo = 2 * vm; hi = lo + 1; }
        else { lo = vm * 2 + BIT(5); if (lo == 31) return NULL; hi = lo + 1; }
        wn *s = vfp_gate();
        if (Ld) { CAT(s, put(rt, ld32(SV(lo)))); CAT(s, put(rt2, ld32(SV(hi)))); }
        else {
            CAT(s, w_store(A, 0x36, SV(lo), -1, L(P_C), reg(rt)));
            CAT(s, w_store(A, 0x36, SV(hi), -1, L(P_C), reg(rt2)));
        }
        return s;
    }
    if (insn & 0x10) {
        unsigned opc1 = insn >> 21 & 7, Ld = BIT(20), rt = vd;
        if ((!dbl && opc1 == 0) || (dbl && opc1 <= 1)) {
            if ((insn & 0x6f) || rt == 15 || (dbl && BIT(7))) return NULL;
            unsigned k = dbl ? vn * 2 + opc1 : vn * 2 + BIT(7);
            wn *s = vfp_gate();
            CAT(s, Ld ? put(rt, ld32(SV(k))) : w_store(A, 0x36, SV(k), -1, L(P_C), reg(rt)));
            return s;
        }
        if (dbl || opc1 != 7 || (insn & 0xef) || vn != 1) return NULL;
        if (Ld) {
            wn *s = vfp_gate();
            if (rt != 15) { CAT(s, put(rt, L(L_FS))); return s; }
            CAT(s, set(L_FN, L(L_FS)));
            CAT(s, set(L_FZ, EQZ(AND(L(L_FS), C(0x40000000)))));
            CAT(s, set(L_FC, AND(SHR(L(L_FS), 29), C(1))));
            CAT(s, set(L_FV, SHL(L(L_FS), 3)));
            return s;
        }
        if (rt == 15) return NULL;
        wn *s = vfp_gate();
        CAT(s, w_store(A, 0x36, OFF[15], -1, L(P_C), AND(reg(rt), C(0xf3f79f9fu))));
        return s;
    }
    unsigned opc = BIT(23) << 2 | BIT(21) << 1 | BIT(20), alt = BIT(6);
#define SREG(f, lo) ((f) * 2 + (lo))
    if (opc != 7) {
        if (opc > 4 || (opc == 4 && alt)) return NULL;
        if (dbl && (BIT(22) || BIT(7) || BIT(5))) return NULL;
        unsigned rd = dbl ? vd : SREG(vd, BIT(22)), rn = dbl ? vn : SREG(vn, BIT(7)),
                 rm = dbl ? vm : SREG(vm, BIT(5)), Z = dbl ? L_DZ : L_SZ;
        wn *(*ld)(unsigned) = dbl ? ldD : ldS;
        wn *s = vfp_gate();
        if (opc == 4) CAT(s, fop(dbl, 3, ld(rn), ld(rm)));
        else if (opc == 3) CAT(s, fop(dbl, alt ? 1 : 0, ld(rn), ld(rm)));
        else if (opc == 2) { CAT(s, fop(dbl, 2, ld(rn), ld(rm))); if (alt) CAT(s, set(Z, fneg(dbl, L(Z)))); }
        else {
            /* 0: VMLA/VMLS, 1: VNMLS/VNMLA */
            bool negp = (opc == 0 && alt) || (opc == 1 && alt), negd = opc == 1;
            CAT(s, fop(dbl, 2, ld(rn), ld(rm)));
            wn *p = negp ? fneg(dbl, L(Z)) : L(Z), *d = negd ? fneg(dbl, ld(rd)) : ld(rd);
            CAT(s, fop(dbl, 0, d, p));
        }
        CAT(s, stF(dbl, rd, L(Z)));
        CAT(s, commit());
        return s;
    }
    if (!alt) return NULL;
    unsigned opc2 = vn, top = BIT(7), D = BIT(22), M = BIT(5);
    if (opc2 == 0 || (opc2 == 1 && !top)) {
        if (dbl && (D || M)) return NULL;
        wn *s = vfp_gate();
        if (dbl) {
            wn *g = w_load(A, 0x29, SV(2 * vm), -1, L(P_C));
            wn *x = opc2 == 1 ? OP2(0x85, g, w_i64(A, INT64_MIN))
                  : top ? OP2(0x83, g, w_i64(A, INT64_MAX)) : g;
            CAT(s, w_store(A, 0x37, SV(2 * vd), -1, L(P_C), x));
            return s;
        }
        wn *g = ld32(SV(SREG(vm, M)));
        wn *x = opc2 == 1 ? XOR(g, C(0x80000000u)) : top ? AND(g, C(0x7fffffff)) : g;
        CAT(s, w_store(A, 0x36, SV(SREG(vd, D)), -1, L(P_C), x));
        return s;
    }
    if (opc2 == 4 || opc2 == 5) {
        bool zero = opc2 == 5;
        if (zero && (vm || M)) return NULL;
        if (dbl && (D || M)) return NULL;
        unsigned VA = dbl ? L_DA : L_SA, VB = dbl ? L_DB : L_SB, UA = dbl ? L_UA : L_A, UB = dbl ? L_UB : L_B;
        unsigned ra = dbl ? vd : SREG(vd, D), rb = dbl ? vm : SREG(vm, M);
#define RAW(k) (dbl ? w_load(A, 0x29, SV(2 * (k)), -1, L(P_C)) : ld32(SV(k)))
#define SNAN(u) (dbl ? AND(OP2(0x51, OP2(0x83, L(u), w_i64(A, INT64_C(0x7ff8000000000000))), w_i64(A, INT64_C(0x7ff0000000000000))), \
                           OP2(0x52, OP2(0x83, L(u), w_i64(A, INT64_C(0x0007ffffffffffff))), w_i64(A, 0))) \
                     : AND(EQ(AND(L(u), C(0x7fc00000)), C(0x7f800000)), NE(AND(L(u), C(0x3fffff)), C(0))))
        wn *s = vfp_gate();
        CAT(s, set(UA, RAW(ra)));
        CAT(s, set(UB, zero ? (dbl ? w_i64(A, 0) : C(0)) : RAW(rb)));
        CAT(s, set(VA, OP1(dbl ? 0xbf : 0xbe, L(UA))));
        CAT(s, set(VB, OP1(dbl ? 0xbf : 0xbe, L(UB))));
        CAT(s, flush(dbl, VA, IDC));
        CAT(s, flush(dbl, VB, IDC));
        uint16_t fne = dbl ? 0x62 : 0x5c, feq = dbl ? 0x61 : 0x5b, flt = dbl ? 0x63 : 0x5d;
        CAT(s, set(L_R, OR(OP2(fne, L(VA), L(VA)), OP2(fne, L(VB), L(VB)))));
        wn *sig = zero ? SNAN(UA) : OR(SNAN(UA), SNAN(UB));
        if (top) sig = OR(sig, L(L_R));
        CAT(s, if_then(sig, set(L_E, OR(L(L_E), C(IOC)))));
        wn *fl = OP3(0x1b, C(0x30000000),
                     OP3(0x1b, C(0x60000000), OP3(0x1b, C(0x80000000u), C(0x20000000), OP2(flt, L(VA), L(VB))),
                         OP2(feq, L(VA), L(VB))),
                     L(L_R));
        CAT(s, w_store(A, 0x36, OFF[15], -1, L(P_C), OR(OR(AND(L(L_FS), C(0x0fffffff)), fl), L(L_E))));
        return s;
#undef RAW
#undef SNAN
    }
    if (opc2 == 7 && top) {
        wn *s;
        if (dbl) {
            if (M) return NULL;
            s = vfp_gate();
            CAT(s, set(L_DA, ldD(vm)));
            CAT(s, flush(true, L_DA, IDC));
            CAT(s, set(L_SZ, OP1(0xb6, L(L_DA))));
            CAT(s, nan_exit(false, L_SZ));
            CAT(s, flush(false, L_SZ, UFC));
            CAT(s, stF(false, SREG(vd, D), L(L_SZ)));
            CAT(s, commit());
            return s;
        }
        if (D) return NULL;
        s = vfp_gate();
        CAT(s, set(L_SA, ldS(SREG(vm, M))));
        CAT(s, flush(false, L_SA, IDC));
        CAT(s, set(L_DZ, OP1(0xbb, L(L_SA))));
        CAT(s, nan_exit(true, L_DZ));
        CAT(s, stF(true, vd, L(L_DZ)));
        CAT(s, commit());
        return s;
    }
    return NULL;
#undef SREG
#undef BIT
}

typedef struct {
    bool has, stop, wf;
    uint32_t pc, w;
    wn *body;
    uint32_t t[64];
    unsigned nt;
} cop;
static cop CODE[512];

void arm_wasm_configure(const uint32_t offsets[16]) {
    memcpy(OFF, offsets, sizeof OFF);
    R = OFF[0];
    F = OFF[1];
    FETCH = OFF[2];
}

static wn *exit_why(int why) { return fail(why); }
static wn *budget_check(void) { return if_then(OP2(0x4f, L(P_N), L(P_BUDGET)), exit_why(1)); }
static wn *mode_ok(bool thumb) { return EQ(AND(L(L_F), C(32)), C(thumb ? 32 : 0)); }
static wn *slot_index(bool thumb) { return SHR(AND(reg(15), C(1023)), thumb ? 1 : 2); }

static wn *link_tail(void) {
    wn *a = L(L_A);
    wn *want = OR(OR(AND(a, C(-1024)), SHR(AND(L(L_F), C(32)), 4)), C(1));
    wn *aligned = EQZ(AND(L(L_A), OP3(0x1b, C(1), C(3), AND(L(L_F), C(32)))));
    wn *tc = w_op(A, 0x13, 4, L(P_C), L(P_BUDGET), L(P_N), w_load(A, 0x28, 4, -1, L(L_P)));
    tc->imm_kind = WI_CALLI;
    tc->imm = 0;
    tc->imm2 = 0;
    wn *inner = if_then(w_call(A, 1, 2, L(P_C), L(L_A)), tc);
    wn *then_ = seq_of(3, set(L_A, L(L_R15)),
        set(L_P, ADD(w_global_get(A, 0), SHL(AND(SHR(L(L_A), 10), C(4095)), 3))),
        if_then(AND(EQ(w_load(A, 0x28, 0, -1, L(L_P)), want), aligned), inner));
    return if_then(AND(OP2(0x49, L(P_N), L(P_BUDGET)),
                       OR(EQ(L(L_WHY), C(2)), EQ(L(L_WHY), C(3)))), then_);
}

typedef struct { unsigned head, n; } blk;

/* Returns 1 with a function body in fn, 0 if the region has no code, -1 on error. */
static int compile_region(const arm_wasm_region *e, wbuf *fn, uint32_t masks[16]) {
    warena_reset(A);
    DONE = w_label(A);
    const unsigned width = e->thumb ? 2 : 4, slots = 1024 / width;
    const bool th = e->thumb;
    memset(CODE, 0, sizeof CODE);
    for (size_t i = 0; i < e->nops; i++) {
        uint32_t d = e->ops[i].pc - e->pc;
        if (d >= 1024 || d % width) return -1;
        CODE[d / width].pc = e->ops[i].pc;
        CODE[d / width].w = e->ops[i].w;
        CODE[d / width].stop = true; /* marks presence in the input */
    }
    for (unsigned i = 0; i < slots; i++) {
        cop *c = &CODE[i];
        if (!c->stop) continue;
        c->stop = false;
        memset(&ST, 0, sizeof ST);
        ST.on = true;
        op_t o;
        if (emit(c->w, c->pc, th, &o)) {
            c->has = true;
            c->body = o.body;
            c->stop = o.stop;
            c->wf = ST.writes_f;
            c->nt = ST.n;
            memcpy(c->t, ST.t, sizeof c->t);
        }
        ST.on = false;
    }
    static bool H[512];
    memset(H, 0, sizeof H);
    for (unsigned i = 0; i < slots; i++) {
        if (!CODE[i].has) continue;
        if (!i || !CODE[i - 1].has || CODE[i - 1].stop) H[i] = true;
        for (unsigned k = 0; k < CODE[i].nt; k++) {
            uint32_t d = CODE[i].t[k] - e->pc;
            if (CODE[i].t[k] != CODE[i].pc + width && d < 1024 && !(d % width) && CODE[d / width].has)
                H[d / width] = true;
        }
    }
    for (unsigned i = 0; i < slots; i++) {
        if (!CODE[i].has || !H[i]) continue;
        unsigned p = i, k = 0;
        while (p < slots && CODE[p].has) {
            if (k++ == 16) { H[p] = true; break; }
            if (CODE[p].stop) break;
            p++;
            if (p < slots && H[p]) break;
        }
    }
    static blk B[512];
    static int lab[512];
    unsigned nb = 0;
    for (unsigned i = 0; i < slots; i++) lab[i] = -1;
    for (unsigned i = 0; i < slots; i++) {
        if (!H[i] || !CODE[i].has) continue;
        unsigned p = i, n = 0;
        while (p < slots && CODE[p].has) {
            lab[p] = (int)nb;
            n++;
            if (CODE[p].stop || n == 16) break;
            p++;
            if (p < slots && H[p]) break;
        }
        B[nb].head = i;
        B[nb].n = n;
        nb++;
    }
    if (!nb) return 0;
    for (unsigned i = 0; i < 16; i++) masks[i] = 0;
    for (unsigned i = 0; i < slots; i++)
        if (CODE[i].has) masks[i >> 5] |= 1u << (i & 31);

    wlabel *run = w_label(A), *unsup = w_label(A);
    wlabel **cases = walloc(A, nb * sizeof *cases);
    unsigned groups = (nb + 63) / 64;
    wlabel *glab[8];
    if (!cases || A->oom || groups > 8) return -1;
    for (unsigned i = 0; i < nb; i++) cases[i] = w_label(A);
    for (unsigned g = 0; g < groups; g++) glab[g] = w_label(A);
    wlabel **tab = walloc(A, slots * sizeof *tab);
    if (!tab || A->oom) return -1;

    wn *top = SEQ();
    CAT(top, put(15, ld32(R + 60)));
    CAT(top, set(L_F, ld32(F)));
    CAT(top, unpack());
    CAT(top, set(L_PRIV, NE(AND(L(L_F), C(31)), C(16))));
    CAT(top, set(L_POFS, OP2(0x6c, L(L_PRIV), C(DREAD_ENTRIES / 2))));

    wn *gcur = NULL;
    if (groups > 1) {
        for (unsigned i = 0; i < slots; i++) tab[i] = lab[i] < 0 ? unsup : glab[lab[i] / 64];
        gcur = seq_of(1, w_br_table(A, tab, slots, unsup, slot_index(th)));
    }
    for (unsigned g = 0; g < groups; g++) {
        unsigned lo = g * 64, hi = nb < lo + 64 ? nb : lo + 64;
        for (unsigned i = 0; i < slots; i++)
            tab[i] = lab[i] >= (int)lo && lab[i] < (int)hi ? cases[lab[i]] : unsup;
        wn *cur = seq_of(1, w_br_table(A, tab, slots, unsup, slot_index(th)));
        for (unsigned bi = lo; bi < hi; bi++) {
            const blk *b = &B[bi];
            const cop *ops = &CODE[b->head];
            wn *code = seq_of(1, w_block(A, cases[bi], cur));
            unsigned vg = L_V0 + (bi >> 6);
            int64_t vb = (int64_t)(UINT64_C(1) << (bi & 63));
            wn *verify = SEQ();
            uint8_t bytes[64];
            unsigned nbytes = 0;
            for (unsigned i = 0; i < b->n; i++)
                for (unsigned k = 0; k < width; k++) bytes[nbytes++] = (uint8_t)(ops[i].w >> (8 * k));
            for (unsigned i = 0; i < nbytes;) {
                unsigned size = nbytes - i >= 8 ? 8 : nbytes - i >= 4 ? 4 : 2;
                uint32_t at = (ops[0].pc & 1023) + i;
                uint64_t v = 0;
                for (int k = (int)size - 1; k >= 0; k--) v = v << 8 | bytes[i + k];
                wn *cmp = size == 8
                    ? OP2(0x52, w_load(A, 0x29, at, -1, FE()), w_i64(A, (int64_t)v))
                    : NE(w_load(A, size == 4 ? 0x28 : 0x2f, at, -1, FE()), C(v));
                CAT(verify, if_then(cmp, exit_why(5)));
                i += size;
            }
            CAT(verify, w_set(A, vg, OP2(0x84, L(vg), w_i64(A, vb))));
            CAT(code, if_then(OP1(0x50, OP2(0x83, L(vg), w_i64(A, vb))), verify));

            wlabel *Lb = w_label(A), *bad = w_label(A);
            wlabel **tails = walloc(A, b->n * sizeof *tails);
            if (!tails || A->oom) return -1;
            for (unsigned i = 0; i < b->n; i++) tails[i] = w_label(A);
            wn *t = seq_of(2, w_br_if(A, tails[0], EQ(reg(15), C(ops[0].pc))),
                           w_br_table(A, tails, b->n, bad,
                                      SHR(SUB(reg(15), C(ops[0].pc)), th ? 1 : 2)));
            for (unsigned i = 0; i < b->n; i++)
                t = seq_of(5, w_block(A, tails[i], t), budget_check(), put(15, C(ops[i].pc)),
                           ops[i].body, w_set(A, P_N, ADD(L(P_N), C(1))));
            const cop *last = &ops[b->n - 1];
            for (unsigned m = 0; m < last->nt; m++) {
                uint32_t tg = last->t[m];
                bool dup = false;
                for (unsigned q = 0; q < m; q++) dup |= last->t[q] == tg;
                if (dup) continue;
                int j = -1;
                if (tg == ops[0].pc) j = (int)bi;
                else for (unsigned k = bi + 1; k < hi; k++)
                    if (CODE[B[k].head].pc == tg) { j = (int)k; break; }
                if (j < 0) continue;
                wn *hit = EQ(reg(15), C(tg));
                CAT(t, w_br_if(A, j == (int)bi ? Lb : cases[j], last->wf ? AND(hit, mode_ok(th)) : hit));
            }
            CAT(t, w_br(A, run));
            CAT(code, w_loop(A, Lb, seq_of(3, w_block(A, bad, t), exit_why(4), NULL)));
            /* seq_of skips NULL; br $done follows exit_why's own br */
            cur = code;
        }
        CAT(cur, w_br(A, unsup));
        if (groups > 1) gcur = seq_of(2, w_block(A, glab[g], gcur), cur);
        else gcur = cur;
    }
    wn *loopbody = seq_of(4, budget_check(),
        if_then(NE(AND(reg(15), C(~1023u)), C(e->pc)), exit_why(2)),
        if_then(NE(AND(L(L_F), C(32)), C(th ? 32 : 0)), exit_why(3)),
        w_block(A, unsup, gcur));
    CAT(loopbody, exit_why(4));
    CAT(top, w_block(A, DONE, seq_of(1, w_loop(A, run, loopbody))));
    CAT(top, w_store(A, 0x37, OFF[7], -1, L(P_C), OP2(0x7c, w_load(A, 0x29, OFF[7], -1, L(P_C)), L(L_RH))));
    CAT(top, w_store(A, 0x37, OFF[8], -1, L(P_C), OP2(0x7c, w_load(A, 0x29, OFF[8], -1, L(P_C)), L(L_WH))));
    CAT(top, w_store(A, 0x36, R + 60, -1, L(P_C), reg(15)));
    CAT(top, w_store(A, 0x36, F, -1, L(P_C), packed()));
    CAT(top, if_then(EQ(L(L_WHY), C(5)), w_call(A, 0, 2, C(e->pc), C(th ? 1 : 0))));
    CAT(top, link_tail());
    CAT(top, L(P_N));
    if (A->oom) return -1;

    wbuf body = {0};
    static const struct { uint8_t n, t; } runs[5] = {{9, 0x7f}, {3, 0x7e}, {8, 0x7f}, {3, 0x7d}, {3, 0x7c}};
    wbuf_u32(&body, 6);
    for (int i = 0; i < 5; i++) { wbuf_u32(&body, runs[i].n); wbuf_byte(&body, runs[i].t); }
    wbuf_u32(&body, 2 + groups);
    wbuf_byte(&body, 0x7e);
    w_encode(&body, top);
    wbuf_byte(&body, 0x0b);
    wbuf_u32(fn, (uint32_t)body.len);
    wbuf_bytes(fn, body.buf, body.len);
    bool bad = body.oom;
    wbuf_free(&body);
    return bad || fn->oom ? -1 : 1;
}

static void put_name(wbuf *b, const char *s) {
    wbuf_u32(b, (uint32_t)strlen(s));
    wbuf_bytes(b, s, strlen(s));
}
static void section(wbuf *out, uint8_t id, wbuf *s) {
    wbuf_byte(out, id);
    wbuf_u32(out, (uint32_t)s->len);
    wbuf_bytes(out, s->buf, s->len);
    if (s->oom) out->oom = true;
    wbuf_free(s);
}

int arm_wasm_compile_pages(const arm_wasm_region *regions, size_t n, uint8_t **wasm,
                           size_t *len, arm_wasm_def *defs, size_t *ndefs) {
    warena arena;
    warena_init(&arena, (size_t)16 << 20); /* largest region needs ~4 MiB */
    A = &arena;
    wbuf code = {0};
    uint32_t count = 0;
    int rc = 0;
    for (size_t i = 0; i < n && !rc; i++) {
        uint32_t masks[16];
        int r = compile_region(&regions[i], &code, masks);
        if (r < 0) rc = -1;
        else if (r > 0) {
            arm_wasm_def *d = &defs[count++];
            d->pc = regions[i].pc;
            d->thumb = regions[i].thumb;
            d->index = count - 1;
            memcpy(d->masks, masks, sizeof masks);
        }
    }
    warena_free(&arena);
    A = NULL;
    if (rc) { wbuf_free(&code); return rc; }

    static const uint8_t magic[8] = {0, 'a', 's', 'm', 1, 0, 0, 0};
    static const uint8_t types[] = {3, 0x60, 3, 0x7f, 0x7f, 0x7f, 1, 0x7f,
                                    0x60, 2, 0x7f, 0x7f, 0,
                                    0x60, 2, 0x7f, 0x7f, 1, 0x7f};
    wbuf out = {0}, s = {0};
    wbuf_bytes(&out, magic, sizeof magic);
    wbuf_bytes(&s, types, sizeof types);
    section(&out, 1, &s);
    wbuf_u32(&s, 4);
    put_name(&s, "env"); put_name(&s, "memory"); wbuf_byte(&s, 2); wbuf_byte(&s, 0); wbuf_u32(&s, 8192);
    put_name(&s, "env"); put_name(&s, "stale"); wbuf_byte(&s, 0); wbuf_u32(&s, 1);
    put_name(&s, "env"); put_name(&s, "refill"); wbuf_byte(&s, 0); wbuf_u32(&s, 2);
    put_name(&s, "env"); put_name(&s, "link"); wbuf_byte(&s, 3); wbuf_byte(&s, 0x7f); wbuf_byte(&s, 0);
    section(&out, 2, &s);
    wbuf_u32(&s, count + 1);
    for (uint32_t i = 0; i <= count; i++) wbuf_u32(&s, 0);
    section(&out, 3, &s);
    wbuf_byte(&s, 1); wbuf_byte(&s, 0x70); wbuf_byte(&s, 0); wbuf_u32(&s, count);
    section(&out, 4, &s);
    wbuf_u32(&s, 1); put_name(&s, "run"); wbuf_byte(&s, 0); wbuf_u32(&s, 2 + count);
    section(&out, 7, &s);
    static const uint8_t elem_head[] = {1, 0, 0x41, 0, 0x0b};
    wbuf_bytes(&s, elem_head, sizeof elem_head);
    wbuf_u32(&s, count);
    for (uint32_t i = 0; i < count; i++) wbuf_u32(&s, 2 + i);
    section(&out, 9, &s);
    static const uint8_t run_body[] = {13, 0, 0x20, 0, 0x20, 1, 0x41, 0, 0x20, 2, 0x11, 0, 0, 0x0b};
    wbuf_u32(&s, count + 1);
    wbuf_bytes(&s, code.buf, code.len);
    wbuf_bytes(&s, run_body, sizeof run_body);
    if (code.oom) s.oom = true;
    wbuf_free(&code);
    section(&out, 10, &s);
    if (out.oom) { wbuf_free(&out); return -1; }
    *wasm = out.buf;
    *len = out.len;
    *ndefs = count;
    return 0;
}
