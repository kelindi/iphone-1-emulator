#include "board.h"
#include "bringup_os1.h"
#include "bringup.h"
#include "lzss.h"
#include "snapshot.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *p; size_t n; } blob_t;
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static blob_t read_blob(const char *path) {
    blob_t b={0}; FILE *f=fopen(path,"rb"); long n;
    if (!f) { fprintf(stderr,"cannot open %s: %s\n",path,strerror(errno)); return b; }
    fseek(f,0,SEEK_END); n=ftell(f); fseek(f,0,SEEK_SET);
    if (n<=0 || (b.p=malloc((size_t)n))==NULL ||
        fread(b.p,1,(size_t)n,f)!=(size_t)n) { free(b.p); b.p=NULL; }
    fclose(f); b.n=b.p?(size_t)n:0;
    if (!b.p) return b;
    if (b.n>=4 && !memcmp(b.p,"8900",4)) {
        size_t off=0x800, sz=b.n>=16?le32(b.p+12):0;
        if (off+sz>b.n) { free(b.p); return (blob_t){0}; }
        uint8_t *q=malloc(sz); memcpy(q,b.p+off,sz); free(b.p); b=(blob_t){q,sz};
        printf("detected 8900 wrapper, payload=%zu\n",b.n);
    }
    if (b.n>=4 && !memcmp(b.p,"2gmI",4)) {
        size_t off=0x400, sz=b.n>=0x1c?le32(b.p+0x18):0;
        if (off+sz>b.n) { free(b.p); return (blob_t){0}; }
        uint8_t *q=malloc(sz); memcpy(q,b.p+off,sz); free(b.p); b=(blob_t){q,sz};
        printf("detected IMG2 wrapper, payload=%zu\n",b.n);
    }
    lzss_header_t h;
    if (lzss_parse_header(b.p,b.n,&h)) {
        uint8_t *q=malloc(h.uncompressed_size);
        if (!q || lzss_decompress(q,h.uncompressed_size,b.p+LZSS_HEADER_SIZE,
                                  h.compressed_size)!=h.uncompressed_size) {
            free(q); free(b.p); return (blob_t){0};
        }
        free(b.p); b=(blob_t){q,h.uncompressed_size};
        printf("detected complzss, decompressed=%zu\n",b.n);
    } else printf("using raw file, bytes=%zu\n",b.n);
    return b;
}
static int save_ppm(const char *path, const s5l8900_t *m) {
    unsigned k=s5l_clcd_active_window(&m->clcd), w=0,h=0; size_t n;
    uint8_t *rgb;
    if (k==CLCD_WIN_NONE) return 0;
    n=320u*480u*3u; rgb=malloc(n);
    if (!rgb || !s5l_clcd_scanout(&m->clcd,k,m->ram,m->ram_base,m->ram_size,
                                  rgb,n,&w,&h)) { free(rgb); return 0; }
    FILE *f=fopen(path,"wb"); if (!f) { free(rgb); return 0; }
    fprintf(f,"P6\n%u %u\n255\n",w,h); fwrite(rgb,1,(size_t)w*h*3u,f);
    fclose(f); free(rgb); return 1;
}
typedef struct { FILE *serial; const s5l8900_t *m; bool bol; } tap_t;
/* Serial output; each line in the --serial file is prefixed with the guest
 * instruction count so kernel messages can be correlated with --swi-log. */
static void tap(void *ctx,uint8_t c) {
    tap_t *t=ctx; putchar((int)c); fflush(stdout);
    if (t->serial) {
        if (t->bol) fprintf(t->serial,"%llu ",(unsigned long long)t->m->cpu.cycles);
        fputc(c,t->serial); fflush(t->serial); t->bol=(c=='\n');
    }
}
static const char *status_name(arm_status_t s) {
    switch(s) { case ARM_OK:return "ARM_OK"; case ARM_UNDEFINED:return "ARM_UNDEFINED";
    case ARM_HALT:return "ARM_HALT"; case ARM_GUEST_UNDEFINED:return "ARM_GUEST_UNDEFINED"; }
    return "other";
}
static bool g_break_hit;
static bool break_hook(void *ctx) { (void)ctx; g_break_hit=true; return false; }
/* --watch-pc: log every hit of up to 15 PCs without stopping. */
static uint32_t g_watch[S5L_PRE_STEP_TARGET_MAX]; static unsigned g_watch_n;
#define PC_LOG_MAX 8u
static uint32_t g_pc_log[PC_LOG_MAX]; static unsigned g_pc_log_n;
static uint32_t g_break_pc; static const s5l8900_t *g_watch_m;
static uint32_t g_exec_pc, g_swi_pc; static FILE *g_swi_log;
static int g_break_swi; static bool g_break_swi_set; static uint32_t g_break_swi_ttbr0;
static bool read_va32(s5l8900_t *m, uint32_t va, uint32_t *out) {
    uint32_t pa;
    if (arm_mmu_translate(&m->cpu, va, ARM_ACCESS_READ, true, &pa)!=0 ||
        pa<m->ram_base || pa-m->ram_base+4u>m->ram_size) return false;
    memcpy(out, m->ram+(pa-m->ram_base), 4); return true;
}
static void print_va_str(FILE *o, s5l8900_t *m, uint32_t va) {
    for (unsigned i=0;i<256;i+=4) { uint32_t w; if (!read_va32(m,va+i,&w)) { fputc('?',o); return; }
        for (unsigned b=0;b<4;b++) { char c=(char)(w>>(8*b)); if (!c) return; fputc(c,o); } }
}
static bool print_va_bytes(s5l8900_t *m, uint32_t va, unsigned len,
                           const char *name) {
    uint8_t bytes[48];
    unsigned n = len > sizeof bytes ? sizeof bytes : len;
    for (unsigned i = 0u; i < n; i += 4u) {
        uint32_t word;
        if (!read_va32(m, va + i, &word)) return false;
        for (unsigned b = 0u; b < 4u && i + b < n; b++)
            bytes[i + b] = (uint8_t)(word >> (8u * b));
    }
    printf(" [%s]=", name);
    for (unsigned i = 0u; i < n; i++) printf("%02x", bytes[i]);
    return true;
}
static void print_pc_log(s5l8900_t *m, uint32_t pc) {
    bool match = false;
    for (unsigned i = 0u; i < g_pc_log_n; i++)
        if (g_pc_log[i] == pc) match = true;
    if (!match) return;
    printf("PCLOG cyc=%llu pc=%08x ttbr0=%08x r0..r3=%08x %08x %08x %08x "
           "ip=%08x sp=%08x lr=%08x",
           (unsigned long long)m->cpu.cycles, pc, m->cpu.cp15.ttbr0,
           m->cpu.r[0], m->cpu.r[1], m->cpu.r[2], m->cpu.r[3],
           m->cpu.r[12], m->cpu.r[13], m->cpu.r[14]);
    print_va_bytes(m, m->cpu.r[0], sizeof (uint8_t[48]), "r0");
    print_va_bytes(m, m->cpu.r[1], sizeof (uint8_t[48]), "r1");
    print_va_bytes(m, pc, sizeof (uint8_t[16]), "pc");
    puts("");
}
static bool watch_hook(void *ctx) {
    (void)ctx; s5l8900_t *m=(s5l8900_t*)g_watch_m; uint32_t pc=m->cpu.r[15]&~1u;
    print_pc_log(m, pc);
    if (g_break_pc && pc==g_break_pc) { g_break_hit=true; return false; }
    if (g_exec_pc && pc==g_exec_pc) {
        /* XNU execve(proc, uap, retval): uap->fname is a user_addr_t whose
         * low word is at [r1]; the caller's map is still current. */
        uint32_t fname=0; read_va32(m,m->cpu.r[1],&fname);
        printf("EXEC cyc=%llu ",(unsigned long long)m->cpu.cycles); print_va_str(stdout,m,fname);
        uint32_t argv=0; read_va32(m,m->cpu.r[1]+4u,&argv);
        for (unsigned a=0; a<6u && argv; a++) {
            uint32_t arg=0;
            if (!read_va32(m,argv+4u*a,&arg) || !arg) break;
            putchar(' '); print_va_str(stdout,m,arg);
        }
        puts("");
        return false;
    }
    if (g_swi_pc && pc==g_swi_pc) {
        /* SWI vector entry (SVC mode): lr = user PC+4, r12 = Darwin syscall
         * number, r0-r3 = args; ttbr0 identifies the address space. */
        fprintf(g_swi_log,"%llu %08x %08x %d %08x %08x %08x %08x\n",(unsigned long long)m->cpu.cycles,
                m->cpu.cp15.ttbr0,m->cpu.r[14]-4u,(int)m->cpu.r[12],m->cpu.r[0],m->cpu.r[1],m->cpu.r[2],m->cpu.r[3]);
        int no=(int)m->cpu.r[12];
        if (g_break_swi_set && no==g_break_swi &&
            (!g_break_swi_ttbr0 || m->cpu.cp15.ttbr0==g_break_swi_ttbr0)) { g_break_hit=true; return false; }
        if (no==-31) { /* mach_msg_trap: header {bits,size,remote,local,reserved,id} */
            uint32_t hb=0,rp=0,lp=0,id=0; read_va32(m,m->cpu.r[0],&hb); read_va32(m,m->cpu.r[0]+8u,&rp);
            read_va32(m,m->cpu.r[0]+12u,&lp); read_va32(m,m->cpu.r[0]+20u,&id);
            fprintf(g_swi_log,"  msg bits=%08x remote=%x local=%x id=%d\n",hb,rp,lp,(int)id);
        } else if (no==5||no==188||no==33||no==59) { /* open, stat, access, execve */
            fputs("  path ",g_swi_log); print_va_str(g_swi_log,m,m->cpu.r[0]); fputc('\n',g_swi_log);
        } else if (no==4||no==133) { /* write, sendto: printable prefix of the buffer */
            uint32_t n=m->cpu.r[2]; if (n>200u) n=200u;
            fputs("  data \"",g_swi_log);
            for (uint32_t i=0;i<n;i++) { uint32_t w=0;
                if (!read_va32(m,(m->cpu.r[1]+i)&~3u,&w)) break;
                int ch=(int)((w>>(8u*((m->cpu.r[1]+i)&3u)))&0xffu);
                fputc(ch>=0x20&&ch<0x7f?ch:'.',g_swi_log); }
            fputs("\"\n",g_swi_log);
        }
        return false;
    }
    printf("W %08x cyc=%llu tb=%llu r0=%08x r1=%08x r2=%08x r3=%08x r5=%08x sp=%08x lr=%08x\n",pc,
           (unsigned long long)m->cpu.cycles,(unsigned long long)m->timer.ticks,
           m->cpu.r[0],m->cpu.r[1],m->cpu.r[2],m->cpu.r[3],m->cpu.r[5],m->cpu.r[13],m->cpu.r[14]);
    return false;
}

#define TRACE_RING (1u<<21)
typedef struct { uint32_t pc, cpsr, r0, r1, r2, r3, lr, sp; } trace_ent_t;
static trace_ent_t g_trace[TRACE_RING]; static unsigned g_trace_n;
static void trace_record(const s5l8900_t *m) {
    trace_ent_t *e=&g_trace[g_trace_n++%TRACE_RING];
    e->pc=m->cpu.r[15]; e->cpsr=m->cpu.cpsr; e->r0=m->cpu.r[0]; e->r1=m->cpu.r[1];
    e->r2=m->cpu.r[2]; e->r3=m->cpu.r[3]; e->lr=m->cpu.r[14]; e->sp=m->cpu.r[13];
}
static void trace_dump(unsigned last) {
    unsigned n=g_trace_n<TRACE_RING?g_trace_n:TRACE_RING; if (last<n) n=last;
    unsigned start=g_trace_n-n;
    for (unsigned i=0;i<n;i++) { const trace_ent_t *e=&g_trace[(start+i)%TRACE_RING];
        printf("T %08x %c%02x r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x lr=%08x\n",e->pc,
               (e->cpsr&0x20)?'t':'a',e->cpsr&0x1f,e->r0,e->r1,e->r2,e->r3,e->sp,e->lr); }
}
/* --dump-va: hex-dump guest memory (translated through the current page
 * tables) when the run ends. */
static void dump_va(s5l8900_t *m, uint32_t va, unsigned words) {
    printf("dump %08x:", va);
    for (unsigned i=0;i<words;i++) {
        uint32_t pa, v=0;
        if (arm_mmu_translate(&m->cpu, va+4u*i, ARM_ACCESS_READ, true, &pa)==0 &&
            pa>=m->ram_base && pa-m->ram_base+4u<=m->ram_size)
            memcpy(&v, m->ram+(pa-m->ram_base), 4);
        else { printf(" ????????"); continue; }
        printf("%s %08x", (i%8==0&&i)?"\n                ":"", v);
    }
    puts("");
}
static void usage(void) {
    puts("iphone1 --board m68|n82 --kernel PATH --devicetree PATH [options]\n"
         "  --cpi N           cycles per retired instruction, 1..16, default 1\n"
         "  --dump-ram FILE  write the guest RAM image when the run ends");
}

typedef enum {
    INPUT_TOUCH,
    INPUT_LIFT,
    INPUT_BUTTON
} input_kind_t;

typedef struct {
    uint64_t step;
    input_kind_t kind;
    unsigned phase;
    unsigned button;
    uint16_t x, y;
    bool pressed;
    bool auto_lift;
} input_event_t;

static bool button_name(const char *name, unsigned *which) {
    static const char *const names[] = {
        "hold", "menu", "volup", "voldown", "ringerab"
    };
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++)
        if (!strcmp(name, names[i])) {
            *which = i;
            return true;
        }
    return false;
}

static int input_compare(const void *a, const void *b) {
    const input_event_t *x = a, *y = b;
    return x->step < y->step ? -1 : x->step != y->step;
}

static bool input_touch(s5l8900_t *m, const input_event_t *event) {
    /* Z1 accepts the complete active contact set and derives its own phases.
     * Supplying a BREAK record there keeps the finger active. Z2 instead
     * accepts explicit phase records, and its empty encoding is unsupported. */
    if (event->kind == INPUT_LIFT ||
        (m->mtz1.present && event->phase == MTZ2_PHASE_BREAK_TOUCH))
        return s5l8900_set_touch(m, NULL, 0u);
    s5l_mt_contact_t contact = {1u, (uint8_t)event->phase,
                               event->x, event->y, 255u, 8u, 8u};
    return s5l8900_set_touch(m, &contact, 1u);
}

int main(int argc,char **argv) {
    const char *board_name=NULL,*kp=NULL,*dp=NULL,*rp=NULL,*cmd=NULL,*serial=NULL,*shot=NULL;
    uint64_t steps=200000000ull; unsigned ram_mb=0,vd=0,cpi=1; bool fb=true,debug=false,trace=false;
    uint32_t break_pc=0; uint64_t trace_from=UINT64_MAX; unsigned trace_last=256;
    uint32_t dump[8]; unsigned dump_n=0;
    const char *unmatch[16]; unsigned unmatch_n=0; uint64_t shot_every=0; uint32_t mmio_watch=0;
    const char *load_path = NULL, *save_path = NULL, *dump_ram = NULL;
    const char *audio_wav = NULL; FILE *wav = NULL; uint64_t wav_frames = 0;
    uint64_t save_step = UINT64_MAX;
    input_event_t events[128]; unsigned event_n = 0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--board")&&i+1<argc) board_name=argv[++i];
        else if (!strcmp(argv[i],"--kernel")&&i+1<argc) kp=argv[++i];
        else if (!strcmp(argv[i],"--devicetree")&&i+1<argc) dp=argv[++i];
        else if (!strcmp(argv[i],"--ramdisk")&&i+1<argc) rp=argv[++i];
        else if (!strcmp(argv[i],"--ram-mb")&&i+1<argc) ram_mb=(unsigned)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--cpi")&&i+1<argc) cpi=(unsigned)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--cmdline")&&i+1<argc) cmd=argv[++i];
        else if (!strcmp(argv[i],"--steps")&&i+1<argc) steps=strtoull(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--no-fb")) fb=false;
        else if (!strcmp(argv[i],"--v-display")&&i+1<argc) vd=(unsigned)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--debug-enabled")) debug=true;
        else if (!strcmp(argv[i],"--serial")&&i+1<argc) serial=argv[++i];
        else if (!strcmp(argv[i],"--screenshot")&&i+1<argc) shot=argv[++i];
        else if (!strcmp(argv[i],"--shot-every")&&i+1<argc) shot_every=strtoull(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--trace-devices")) trace=true;
        else if (!strcmp(argv[i],"--mmio-watch")&&i+1<argc) mmio_watch=(uint32_t)strtoul(argv[++i],NULL,0)&~0xfffu;
        else if (!strcmp(argv[i],"--save-at")&&i+2<argc) {
            save_step = strtoull(argv[++i], NULL, 0);
            save_path = argv[++i];
        }
        else if (!strcmp(argv[i],"--load")&&i+1<argc) load_path=argv[++i];
        else if (!strcmp(argv[i],"--audio-wav")&&i+1<argc) audio_wav=argv[++i];
        else if (!strcmp(argv[i],"--at")&&i+2<argc) {
            input_event_t e = { .step = strtoull(argv[++i], NULL, 0) };
            const char *action = argv[++i];
            if (event_n >= sizeof events / sizeof events[0]) {
                fprintf(stderr, "too many --at events\n");
                return 2;
            }
            if (!strcmp(action, "tap") && i + 2 < argc) {
                e.kind = INPUT_TOUCH;
                e.phase = MTZ2_PHASE_MAKE_TOUCH;
                e.x = (uint16_t)strtoul(argv[++i], NULL, 0);
                e.y = (uint16_t)strtoul(argv[++i], NULL, 0);
                e.auto_lift = true;
                events[event_n++] = e;
            } else if (!strcmp(action, "touch") && i + 3 < argc) {
                const char *phase = argv[++i];
                e.kind = INPUT_TOUCH;
                e.phase = !strcmp(phase, "down") ? MTZ2_PHASE_MAKE_TOUCH :
                           !strcmp(phase, "move") ? MTZ2_PHASE_TOUCHING :
                           !strcmp(phase, "up") ? MTZ2_PHASE_BREAK_TOUCH : 255u;
                e.x = (uint16_t)strtoul(argv[++i], NULL, 0);
                e.y = (uint16_t)strtoul(argv[++i], NULL, 0);
                if (e.phase == 255u) {
                    fprintf(stderr, "unknown touch phase: %s\n", phase);
                    return 2;
                }
                events[event_n++] = e;
            } else if (!strcmp(action, "button") && i + 3 < argc) {
                const char *name = argv[++i], *state = argv[++i];
                if (!button_name(name, &e.button) ||
                    (strcmp(state, "down") && strcmp(state, "up"))) {
                    fprintf(stderr, "invalid button action\n");
                    return 2;
                }
                e.kind = INPUT_BUTTON;
                e.pressed = !strcmp(state, "down");
                events[event_n++] = e;
            } else if (!strcmp(action, "press") && i + 1 < argc) {
                const char *name = argv[++i];
                if (!button_name(name, &e.button)) {
                    fprintf(stderr, "unknown button: %s\n", name);
                    return 2;
                }
                e.kind = INPUT_BUTTON;
                e.pressed = true;
                e.auto_lift = true;
                events[event_n++] = e;
            } else {
                usage();
                return 2;
            }
        }
        else if (!strcmp(argv[i],"--verbose")) { }
        else if (!strcmp(argv[i],"--break-pc")&&i+1<argc) break_pc=(uint32_t)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--watch-pc")&&i+1<argc&&g_watch_n<S5L_PRE_STEP_TARGET_MAX-1u)
            g_watch[g_watch_n++]=(uint32_t)strtoul(argv[++i],NULL,0)&~1u;
        else if (!strcmp(argv[i],"--trace-from")&&i+1<argc) trace_from=strtoull(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--trace-last")&&i+1<argc) trace_last=(unsigned)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--unmatch")&&i+1<argc&&unmatch_n<16) unmatch[unmatch_n++]=argv[++i];
        else if (!strcmp(argv[i],"--swi-log")&&i+2<argc&&g_watch_n<S5L_PRE_STEP_TARGET_MAX-1u) {
            g_watch[g_watch_n++]=g_swi_pc=(uint32_t)strtoul(argv[++i],NULL,0)&~1u;
            g_swi_log=fopen(argv[++i],"w"); if (!g_swi_log) { perror("--swi-log"); return 1; } }
        else if (!strcmp(argv[i],"--exec-pc")&&i+1<argc&&g_watch_n<S5L_PRE_STEP_TARGET_MAX-1u)
            g_watch[g_watch_n++]=g_exec_pc=(uint32_t)strtoul(argv[++i],NULL,0)&~1u;
        else if (!strcmp(argv[i],"--pc-log")&&i+1<argc) {
            if (g_pc_log_n >= PC_LOG_MAX ||
                g_watch_n >= S5L_PRE_STEP_TARGET_MAX - 1u) {
                fprintf(stderr, "too many --pc-log targets\n");
                return 2;
            }
            g_pc_log[g_pc_log_n++] =
                (uint32_t)strtoul(argv[++i],NULL,0)&~1u;
            g_watch[g_watch_n++] = g_pc_log[g_pc_log_n - 1u];
        }
        else if (!strcmp(argv[i],"--break-swi")&&i+2<argc) { g_break_swi_set=true;
            g_break_swi=(int)strtol(argv[++i],NULL,0); g_break_swi_ttbr0=(uint32_t)strtoul(argv[++i],NULL,0); }
        else if (!strcmp(argv[i],"--dump-va")&&i+1<argc&&dump_n<8) dump[dump_n++]=(uint32_t)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--dump-ram")&&i+1<argc) dump_ram=argv[++i];
        else { usage(); return 2; }
    }
    const s5l_board_t *b=s5l_board_find(board_name);
    if (!b||!kp||!dp) { usage(); return 2; }
    blob_t k=read_blob(kp), d=read_blob(dp), rd=rp?read_blob(rp):(blob_t){0};
    if (!k.p||!d.p||(rp&&!rd.p)) { free(k.p);free(d.p);free(rd.p);return 1; }
    char why[256];
    if (!s5l_board_check_devicetree(b,d.p,d.n,why,sizeof why)) {
        fprintf(stderr,"device tree rejected: %s\n",why); return 1;
    }
    s5l8900_t m;
    if (!s5l_board_init(b,&m,ram_mb?ram_mb<<20:0)) {
        fprintf(stderr,"machine init failed (RAM may overlap a device window)\n");
        return 1;
    }
    m.trace_devices=trace; m.mtz1.trace=trace; m.mmio_watch_page=mmio_watch;
    s5l_os1_request_t q={k.p,k.n,d.p,d.n,rd.p,rd.n,cmd,fb,vd,b->lcd_width,b->lcd_height,debug,
                         unmatch,unmatch_n};
    s5l_os1_result_t r;
    if (!s5l_os1_boot(&m,&q,&r)) { fprintf(stderr,"boot failed: %s\n",r.detail);
        s5l8900_free(&m); return 1; }
    printf("layout virt_base=0x%08x phys_base=0x%08x ram_size=0x%08x\n",
           r.virt_base,r.phys_base,r.ram_size);
    printf("kernel_begin_pa=0x%08x kernel_end_pa=0x%08x entry_pa=0x%08x entry_va=0x%08x\n",
           r.kernel_begin_pa,r.kernel_end_pa,r.entry_pa,r.entry_va);
    printf("devicetree_pa=0x%08x devicetree_va=0x%08x devicetree_size=0x%08x\n",
           r.devicetree_pa,r.devicetree_va,r.devicetree_size);
    printf("boot_args_pa=0x%08x ramdisk_pa=0x%08x ramdisk_va=0x%08x ramdisk_size=0x%08x\n",
           r.boot_args_pa,r.ramdisk_pa,r.ramdisk_va,r.ramdisk_size);
    printf("framebuffer_pa=0x%08x vram_bytes=0x%08x top_of_kernel_data_pa=0x%08x cmdline=\"%s\"\n",
           r.framebuffer_pa,r.vram_bytes,r.top_of_kernel_data_pa,r.cmdline);
    if (load_path) {
        snapshot_status_t ss = snapshot_load(&m, load_path);
        if (ss != SNAP_OK) {
            fprintf(stderr, "snapshot load failed: %s\n", snapshot_strerror(ss));
            s5l8900_free(&m); free(k.p); free(d.p); free(rd.p);
            return 1;
        }
        fprintf(stderr, "loaded snapshot %s at step %llu\n", load_path,
                (unsigned long long)m.cpu.cycles);
    }
    if (!s5l8900_set_cycles_per_insn(&m, cpi)) {
        fprintf(stderr, "invalid --cpi %u (expected 1..16)\n", cpi);
        s5l8900_free(&m); free(k.p); free(d.p); free(rd.p);
        return 2;
    }
    qsort(events, event_n, sizeof events[0], input_compare);
    tap_t t={serial?fopen(serial,"wb"):NULL,&m,true}; s5l8900_set_uart0_host(&m,tap,&t);
    arm_status_t st=ARM_OK; uint64_t total=m.cpu.cycles; uint32_t ring[32]={0}; unsigned ri=0, same=0;
    unsigned next_event = 0; bool saved = false;
    bool tap_pending = false, press_pending = false;
    uint64_t tap_down_ticks = 0, press_down_ticks = 0;
    unsigned press_button = 0;
    const uint64_t tap_duration_ticks = m.tb_hz ? m.tb_hz / 10u : 600000u;
    uint32_t last_window=0; const char *reason="steps exhausted";
    if (g_watch_n) { g_watch_m=&m; g_break_pc=break_pc&~1u;
        if (g_break_pc) g_watch[g_watch_n++]=g_break_pc;
        if (!s5l8900_set_pre_step_hook(&m,watch_hook,NULL,g_watch,g_watch_n)) fprintf(stderr,"watch hook refused\n"); }
    else if (break_pc) { uint32_t tgt=break_pc&~1u;
        if (!s5l8900_set_pre_step_hook(&m,break_hook,NULL,&tgt,1)) fprintf(stderr,"break hook refused\n"); }
    while (total<steps) {
        if (!saved && save_path && total >= save_step) {
            snapshot_status_t ss = snapshot_save(&m, save_path);
            if (ss != SNAP_OK) fprintf(stderr, "snapshot save failed: %s\n",
                                       snapshot_strerror(ss));
            else fprintf(stderr, "saved snapshot %s at step %llu\n", save_path,
                         (unsigned long long)total);
            saved = true;
        }
        if (tap_pending &&
            m.timer.ticks - tap_down_ticks >= tap_duration_ticks) {
            bool ok = s5l8900_set_touch(&m, NULL, 0u);
            fprintf(stderr, "input step=%llu touch up -> %s\n",
                    (unsigned long long)total, ok ? "accepted" : "rejected");
            tap_pending = false;
            continue;
        }
        if (press_pending &&
            m.timer.ticks - press_down_ticks >= tap_duration_ticks) {
            bool ok = s5l8900_set_button(&m, press_button, false);
            fprintf(stderr, "input step=%llu button %u up -> %s\n",
                    (unsigned long long)total, press_button,
                    ok ? "accepted" : "rejected");
            press_pending = false;
            continue;
        }
        unsigned slice=(unsigned)((steps-total)>1000000ull?1000000ull:steps-total);
        if (next_event < event_n && events[next_event].step <= total) {
            input_event_t *e = &events[next_event++];
            fprintf(stderr, "input step=%llu ", (unsigned long long)total);
            if (e->kind == INPUT_TOUCH || e->kind == INPUT_LIFT) {
                bool ok = input_touch(&m, e);
                fprintf(stderr, "%s %u %u -> %s\n",
                        e->kind == INPUT_LIFT || e->phase == MTZ2_PHASE_BREAK_TOUCH ?
                            "touch up" : "touch",
                        e->x, e->y, ok ? "accepted" : "rejected");
                if (ok && e->auto_lift) {
                    tap_pending = true;
                    tap_down_ticks = m.timer.ticks;
                }
            } else {
                bool ok = s5l8900_set_button(&m, e->button, e->pressed);
                fprintf(stderr, "button %u %s -> %s\n", e->button,
                        e->pressed ? "down" : "up", ok ? "accepted" : "rejected");
                if (ok && e->auto_lift) {
                    press_pending = true;
                    press_button = e->button;
                    press_down_ticks = m.timer.ticks;
                }
            }
            continue;
        }
        uint64_t boundary = steps;
        if (next_event < event_n && events[next_event].step < boundary)
            boundary = events[next_event].step;
        if (!saved && save_path && save_step < boundary) boundary = save_step;
        if (boundary > total && boundary - total < slice) slice = (unsigned)(boundary - total);
        /* trace_from is in retired instructions (cpu.cycles), the unit the
         * EXEC/SWI logs report. */
        if (m.cpu.cycles>=trace_from) { slice=1; trace_record(&m); }
        else if (m.cpu.cycles+slice>trace_from) slice=(unsigned)(trace_from-m.cpu.cycles);
        unsigned n=s5l8900_run(&m,slice,&st); total+=n; ring[ri++%32]=m.cpu.r[15];
        if (audio_wav && !wav) {
            wav = fopen(audio_wav, "wb");
            if (!wav) { fprintf(stderr, "cannot open %s\n", audio_wav); audio_wav = NULL; }
            else { uint8_t z[44] = {0}; fwrite(z, 1, sizeof z, wav); }
        }
        if (wav) {
            int16_t pcm[2048];
            size_t got;
            while ((got = s5l8900_audio_read(&m, pcm, 1024u)) != 0u) {
                fwrite(pcm, 4u, got, wav);
                wav_frames += got;
            }
        }
        if (!saved && save_path && total >= save_step) {
            snapshot_status_t ss = snapshot_save(&m, save_path);
            if (ss != SNAP_OK) fprintf(stderr, "snapshot save failed: %s\n",
                                       snapshot_strerror(ss));
            else fprintf(stderr, "saved snapshot %s at step %llu\n", save_path,
                         (unsigned long long)total);
            saved = true;
        }
        if (shot&&shot_every&&total/shot_every!=(total-n)/shot_every) {
            char p[512]; snprintf(p,sizeof p,"%s.%llu.ppm",shot,(unsigned long long)(total/shot_every));
            if (!save_ppm(p,&m)) printf("periodic screenshot %s: no active CLCD window\n",p);
        }
        if (g_break_hit) { reason="break-pc"; break; }
        uint32_t win=m.cpu.r[15]&~63u;
        if (win==last_window) same++; else same=0; last_window=win;
        if (st!=ARM_OK) { reason=status_name(st); break; }
        if (same>=20 && slice>1) { reason="spin"; break; }
        if (!n) break;
    }
    printf("\nreason=%s instructions=%llu pc=0x%08x cpsr=0x%08x\n",reason,
           (unsigned long long)total,m.cpu.r[15],m.cpu.cpsr);
    for (unsigned i=0;i<16;i++) printf("r%u=0x%08x%s",i,m.cpu.r[i],i%4==3?"\n":" ");
    printf("ttbr0=0x%08x sctlr=0x%08x M=%u unmapped_reads=%llu unmapped_writes=%llu\n",
           m.cpu.cp15.ttbr0,m.cpu.cp15.sctlr,(m.cpu.cp15.sctlr&1u)!=0,
           (unsigned long long)m.unmapped_reads,(unsigned long long)m.unmapped_writes);
    if (m.unmapped_addr_count) {
        printf("unmapped-pages:");
        for (unsigned i=0;i<m.unmapped_addr_count;i++) printf(" 0x%08x",m.unmapped_addr[i]);
        puts("");
    }
    printf("last-pcs:");
    unsigned ring_n = ri < 32u ? ri : 32u;
    unsigned ring_start = ri < 32u ? 0u : ri % 32u;
    for (unsigned i=0;i<ring_n;i++)
        printf(" 0x%08x",ring[(ring_start+i)%32u]);
    puts("");
    if (g_trace_n) trace_dump(trace_last);
    for (unsigned i=0;i<dump_n;i++) dump_va(&m,dump[i],64);
    if (trace) {
        printf("mmio-census (%u):", m.dev_count);
        for (unsigned i=0;i<m.dev_count;i++)
            printf(" %c0x%08x=0x%08x",m.dev_is_write[i]?'W':'R',
                   m.dev_addr[i],m.dev_value[i]);
        puts("");
    }
    if (st!=ARM_OK) {
        uint32_t pc=m.cpu.r[15], off=pc-m.ram_base;
        if (pc>=m.ram_base&&off+4<=m.ram_size)
            printf("bytes-at-pc=%02x%02x%02x%02x\n",m.ram[off],m.ram[off+1],m.ram[off+2],m.ram[off+3]);
    }
    for (unsigned i=0;i<S5L8900_I2S_COUNT;i++) {
        const s5l_i2s_stream_t *a=&m.i2s_tx[i];
        if (!a->tx_stores && !a->lrck_rising) continue;
        printf("i2s%u-tx: lrck_rising=%llu stores=%llu shifted=%llu underruns=%llu overflow=%llu fifo=%u\n",
               i,(unsigned long long)a->lrck_rising,(unsigned long long)a->tx_stores,
               (unsigned long long)a->tx_bytes,(unsigned long long)a->underruns,
               (unsigned long long)a->overflow_bytes,a->fifo_level);
    }
    if (wav) {
        uint32_t rate = s5l8900_audio_rate(&m), data = (uint32_t)(wav_frames * 4u);
        uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
                         16,0,0,0,1,0,2,0,0,0,0,0,0,0,0,0,4,0,16,0,
                         'd','a','t','a',0,0,0,0};
        uint32_t riff = 36u + data, br = rate * 4u;
        memcpy(h+4,&riff,4); memcpy(h+24,&rate,4); memcpy(h+28,&br,4); memcpy(h+40,&data,4);
        fseek(wav, 0, SEEK_SET); fwrite(h, 1, sizeof h, wav); fclose(wav);
        printf("audio-wav: %s frames=%llu dropped=%llu\n", audio_wav,
               (unsigned long long)wav_frames, (unsigned long long)m.audio_dropped);
    }
    if (shot&&!save_ppm(shot,&m)) fprintf(stderr,"screenshot failed: %s\n",shot);
    if (dump_ram) {
        FILE *rf=fopen(dump_ram,"wb");
        if (!rf||fwrite(m.ram,1,m.ram_size,rf)!=m.ram_size) fprintf(stderr,"ram dump failed: %s\n",dump_ram);
        if (rf) fclose(rf);
    }
    if (t.serial) fclose(t.serial); s5l8900_free(&m); free(k.p);free(d.p);free(rd.p); return 0;
}
