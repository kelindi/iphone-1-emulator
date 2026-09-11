#include "bringup_os1.h"
#include "bringup.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x, ...) do { if (!(x)) { printf("FAIL: " __VA_ARGS__); failures++; } } while (0)
static void p32(uint8_t *p, uint32_t v) {
    p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24;
}

/* The fixture is intentionally small; it exercises the loader's layout and
 * argument ABI without embedding Apple firmware. */
static size_t make_macho(uint8_t *b) {
    memset(b, 0, 512);
    p32(b, MH_MAGIC_32); p32(b+4, MH_CPU_TYPE_ARM); p32(b+8, 6);
    p32(b+12, MH_EXECUTE); p32(b+16, 2); p32(b+20, 56+84);
    p32(b+28, LC_SEGMENT); p32(b+32, 56); memcpy(b+36, "__TEXT", 6);
    p32(b+52, 0xc0000000u); p32(b+56, 0x1000); p32(b+60, 0x100); 
    p32(b+64, 0x100); p32(b+68, 0x1000); p32(b+76, 0);
    p32(b+84, LC_UNIXTHREAD); p32(b+88, 84); p32(b+100+15*4, 0xc0000010u);
    return 512;
}

static size_t prop(uint8_t *b, size_t at, const char *name, const void *v, size_t n) {
    memset(b+at, 0, 36); strncpy((char *)(b+at), name, 32); p32(b+at+32, n);
    memcpy(b+at+36, v, n); return at + 36 + ((n+3)&~3u);
}
static size_t make_dt(uint8_t *b) {
    uint8_t comp[]="M68AP\0"; uint8_t f[4]; size_t o=0;
    memset(b,0,4096);
    p32(b, 3); p32(b+4, 4); o=8;
    o=prop(b,o,"compatible",comp,sizeof comp);
    p32(f,0); o=prop(b,o,"clock-frequency",f,4);
    o=prop(b,o,"name","",1);
    p32(b+o,1); p32(b+o+4,1); o+=8;
    o=prop(b,o,"name","cpus",5);
    p32(b+o,7); p32(b+o+4,0); o+=8;
    o=prop(b,o,"name","cpu0",5);
    static const char *names[]={"timebase-frequency","clock-frequency",
        "bus-frequency","memory-frequency","peripheral-frequency","fixed-frequency"};
    for (unsigned i=0;i<6;i++) o=prop(b,o,names[i],f,4);
    p32(b+o,2); p32(b+o+4,0); o+=8;
    o=prop(b,o,"name","memory",7); uint8_t reg[8]={0}; o=prop(b,o,"reg",reg,8);
    p32(b+o,2); p32(b+o+4,0); o+=8;
    o=prop(b,o,"name","vram",5); o=prop(b,o,"reg",reg,8);
    p32(b+o,2); p32(b+o+4,1); o+=8;
    o=prop(b,o,"name","chosen",7);
    p32(f,0); o=prop(b,o,"debug-enabled",f,4);
    p32(b+o,2); p32(b+o+4,0); o+=8;
    o=prop(b,o,"name","memory-map",11);
    o=prop(b,o,"MemoryMapReserved-0",reg,8);
    return o;
}

int main(void) {
    uint8_t macho[512], dt[4096]; size_t ml=make_macho(macho);
    (void)make_dt(dt);
    s5l8900_t m; memset(&m, 0, sizeof m);
    CHECK(s5l8900_init_soc(&m, 0x08000000u, 256u<<20), "init\n");
    uint8_t rd[123]; memset(rd, 0xa5, sizeof rd);
    s5l_os1_request_t q = { macho, ml, dt, make_dt(dt), rd, sizeof rd,
                            "debug=1", true, 2, 320, 480, true };
    s5l_os1_result_t r; CHECK(s5l_os1_boot(&m,&q,&r), "boot failed: %s\n", r.detail);
    CHECK(r.ok && r.devicetree_pa < r.boot_args_pa, "layout overlap\n");
    CHECK(m.cpu.r[15] == r.entry_pa && m.cpu.r[0] == r.boot_args_pa, "register ABI\n");
    CHECK(r.ramdisk_va == r.virt_base + r.ramdisk_pa - r.phys_base, "RAMDisk alias\n");
    CHECK(r.framebuffer_pa && r.vram_bytes, "framebuffer layout\n");
    s5l8900_free(&m); printf("bringup_os1: %s\n", failures ? "failed":"passed");
    return failures != 0;
}
