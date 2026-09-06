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

static bool own_seen, own_pass_written, own_serial_pass, own_exit_seen, own_failed;
static uint32_t own_ttbr, own_exit_status; static unsigned own_entries;
static char own_line[256]; static unsigned own_line_n;
static const unsigned char own_text[]={240,64,45,233,12,112,141,226,0,13,45,233,34,221,77,226,7,208,205,227,0,10,159,229,0,32,160,227,236,25,159,229,92,32,141,229,88,32,141,229,127,32,160,227,1,36,130,227,84,32,141,229,80,16,141,229,160,2,0,235,220,73,159,229,2,0,160,227,1,16,160,227,0,32,160,227,0,48,148,229,51,255,47,225,0,176,160,225,1,0,112,227,215,0,0,218,16,64,141,229,188,9,159,229,11,16,160,225,0,32,160,227,202,2,0,235,176,41,159,229,176,169,159,229,160,9,159,229,140,89,159,229,128,64,141,226,48,0,141,229,160,9,159,229,2,107,160,227,96,128,141,226,40,0,141,229,148,9,159,229,0,16,160,227,36,0,141,229,140,9,159,229,28,0,141,229,136,9,159,229,52,176,141,229,12,64,141,229,24,0,141,229,32,32,141,229,2,0,160,225,0,32,160,227,44,16,141,229,178,2,0,235,0,48,154,229,56,96,141,229,56,96,141,226,11,0,160,225,5,16,160,225,6,32,160,225,60,64,141,229,51,255,47,225,0,64,160,225,1,0,112,227,3,0,0,202,48,0,157,229,0,0,144,229,48,255,47,225,0,96,160,225,0,32,150,229,40,0,157,229,4,16,160,225,159,2,0,235,0,0,84,227,56,32,157,85,128,64,141,226,0,0,82,83,141,0,0,74,0,0,160,227,162,5,80,225,138,0,0,26,36,0,157,229,4,16,160,225,79,3,0,235,56,0,157,229,0,0,80,227,81,0,0,10,0,80,160,227,0,16,160,227,20,16,141,229,5,0,64,224,32,0,80,227,126,0,0,58,5,96,132,224,16,32,160,227,16,16,214,229,16,0,81,227,2,16,160,145,16,176,129,226,0,0,91,225,118,0,0,138,0,0,160,227,124,0,141,229,120,0,141,229,116,0,141,229,112,0,141,229,108,0,141,229,104,0,141,229,100,0,141,229,96,0,141,229,0,16,214,231,0,16,200,231,1,0,128,226,16,0,80,227,250,255,255,26,0,48,154,229,52,0,157,229,64,24,159,229,8,32,160,225,51,255,47,225,0,64,160,225,0,0,80,227,1,0,0,74,176,39,221,225,3,0,0,234,48,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,60,8,159,229,4,16,160,225,100,2,0,235,52,8,159,229,6,16,160,225,16,32,160,227,27,3,0,235,0,0,84,227,1,0,0,10,128,64,141,226,14,0,0,234,112,0,221,229,128,64,141,226,1,0,16,227,10,0,0,10,17,0,214,229,2,0,80,227,7,0,0,26,16,0,214,229,8,0,80,227,4,0,0,58,20,0,214,229,127,0,80,227,21,0,214,5,0,0,80,3,4,0,0,10,56,0,157,229,5,80,139,224,5,0,80,225,186,255,255,138,10,0,0,234,22,0,214,229,0,0,80,227,247,255,255,26,23,0,214,229,20,16,157,229,1,0,80,227,0,16,160,1,20,16,141,229,241,255,255,234,52,176,157,229,3,0,0,234,20,0,157,229,52,176,157,229,1,0,80,227,89,0,0,10,44,0,157,229,80,87,159,229,0,0,80,227,3,0,0,10,2,107,160,227,28,0,80,227,2,0,0,154,27,0,0,234,104,2,0,235,2,107,160,227,28,0,157,229,0,16,144,229,9,13,160,227,61,9,128,227,49,255,47,225,0,64,160,225,0,0,80,227,5,0,0,74,24,0,157,229,4,16,160,225,0,32,160,227,35,2,0,235,128,64,141,226,11,0,0,234,48,0,157,229,0,0,144,229,48,255,47,225,0,96,144,229,24,0,157,229,4,16,160,225,6,32,160,225,25,2,0,235,4,0,86,227,2,107,160,227,128,64,141,226,6,0,0,26,44,16,157,229,32,32,157,229,1,16,129,226,30,0,81,227,89,255,255,26,232,6,159,229,214,1,0,235,70,96,160,227,1,0,0,234,70,96,160,227,52,176,157,229,0,0,91,227,24,0,0,74,20,7,159,229,0,16,144,229,11,0,160,225,49,255,47,225,0,64,160,225,0,32,160,227,1,0,112,227,4,0,0,202,48,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,70,96,160,227,244,6,159,229,4,16,160,225,248,1,0,235,7,0,0,234,92,6,159,229,0,0,144,229,48,255,47,225,0,32,144,229,80,6,159,229,11,16,160,225,240,1,0,235,70,96,160,227,200,6,159,229,200,22,159,229,0,32,144,229,196,6,159,229,0,0,82,227,71,96,160,19,0,0,86,227,1,0,160,1,174,1,0,235,176,6,159,229,0,16,144,229,6,0,160,225,49,255,47,225,24,208,71,226,0,13,189,232,240,128,189,232,22,2,0,235,70,96,160,227,0,0,80,227,208,255,255,10,11,0,160,225,69,2,0,235,0,0,80,227,204,255,255,10,8,6,159,229,80,128,141,226,8,16,160,225,16,32,160,227,0,48,144,229,11,0,160,225,16,160,160,227,51,255,47,225,0,64,160,225,1,0,112,227,144,0,0,218,224,5,159,229,4,16,160,225,0,32,160,227,0,80,160,227,199,1,0,235,208,5,159,229,108,80,141,229,104,80,141,229,100,80,141,229,96,80,141,229,52,176,157,229,0,48,144,229,76,64,141,226,96,16,141,226,11,0,160,225,4,32,160,225,76,160,141,229,51,255,47,225,0,80,160,225,1,0,112,227,3,0,0,202,48,0,157,229,0,0,144,229,48,255,47,225,0,64,160,225,132,5,159,229,0,32,148,229,5,16,160,225,175,1,0,235,120,5,159,229,96,160,141,226,10,16,160,225,16,32,160,227,101,2,0,235,0,0,85,227,157,255,255,74,76,0,157,229,16,0,80,227,154,255,255,26,80,0,221,229,96,16,221,229,0,0,81,225,150,255,255,26,1,0,160,227,8,0,80,227,7,0,0,10,0,16,216,231,0,32,218,231,1,0,128,226,1,0,82,225,248,255,255,10,2,0,64,226,7,0,80,227,139,255,255,58,24,5,159,229,1,16,160,227,0,32,144,229,11,0,160,225,50,255,47,225,0,64,160,225,1,0,112,227,90,0,0,218,252,4,159,229,4,16,160,225,0,32,160,227,138,1,0,235,16,0,157,229,1,16,160,227,0,32,160,227,0,48,144,229,2,0,160,227,51,255,47,225,0,160,160,225,1,0,112,227,83,0,0,218,204,4,159,229,10,16,160,225,0,32,160,227,0,128,160,227,124,1,0,235,10,0,160,225,230,1,0,235,70,96,160,227,0,0,80,227,253,0,0,10,168,4,159,229,80,16,141,226,16,32,160,227,0,48,144,229,10,0,160,225,51,255,47,225,0,64,160,225,1,0,112,227,71,0,0,218,136,4,159,229,4,16,160,225,0,32,160,227,106,1,0,235,52,176,157,229,1,16,160,227,11,0,160,225,89,2,0,235,0,128,160,227,0,0,80,227,233,0,0,10,104,4,159,229,0,16,160,227,68,16,141,229,64,16,141,229,60,16,141,229,56,16,141,229,0,48,144,229,16,16,160,227,76,64,141,226,76,16,141,229,56,16,141,226,11,0,160,225,4,32,160,225,51,255,47,225,0,80,160,225,1,0,112,227,3,0,0,202,48,0,157,229,0,0,144,229,48,255,47,225,0,64,160,225,24,4,159,229,0,32,148,229,5,16,160,225,74,1,0,235,12,4,159,229,56,96,141,226,6,16,160,225,16,32,160,227,0,2,0,235,0,0,85,227,4,0,0,74,76,0,157,229,16,0,80,227,57,0,221,5,2,0,80,3,76,0,0,10,0,0,85,227,174,0,0,90,188,0,0,234,48,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,140,3,159,229,4,16,160,225,52,1,0,235,40,255,255,234,48,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,128,3,159,229,246,255,255,234,48,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,108,3,159,229,10,16,160,225,240,255,255,234,48,0,157,229,0,0,144,229,48,255,47,225,0,128,144,229,88,3,159,229,4,16,160,225,8,32,160,225,30,1,0,235,36,0,88,227,39,0,0,26,10,0,160,225,4,16,160,227,4,64,160,227,11,2,0,235,0,0,80,227,33,0,0,10,44,3,159,229,76,64,141,229,0,16,224,227,7,32,160,227,0,80,144,229,128,16,141,229,255,16,160,227,76,0,141,226,0,0,141,229,255,28,129,227,1,42,130,227,10,0,160,225,128,48,141,226,53,255,47,225,0,64,160,225,1,0,112,227,3,0,0,202,48,0,157,229,0,0,144,229,48,255,47,225,12,0,141,229,220,2,159,229,12,16,157,229,0,32,145,229,4,16,160,225,252,0,0,235,0,0,84,227,5,0,0,74,76,0,157,229,4,0,80,227,2,0,0,26,128,0,157,229,0,0,80,227,137,255,255,10,52,176,157,229,0,128,160,227,119,0,0,234,84,0,221,229,60,16,221,229,0,0,81,225,95,0,0,26,5,0,160,227,80,48,141,226,8,0,80,227,7,0,0,10,0,16,211,231,0,32,214,231,1,0,128,226,1,0,82,225,248,255,255,10,6,0,64,226,3,0,80,227,162,255,255,58,5,0,160,225,75,1,0,235,0,0,80,227,158,255,255,10,92,98,159,229,10,0,160,225,54,32,160,227,6,16,160,225,8,2,0,235,0,0,80,227,151,255,255,10,128,176,141,226,5,0,160,225,11,16,160,225,54,32,160,227,0,48,160,227,83,2,0,235,0,0,80,227,143,255,255,10,128,0,221,229,71,0,80,227,140,255,255,26,1,0,160,227,54,0,80,227,7,0,0,10,0,16,219,231,0,32,214,231,1,0,128,226,2,0,81,225,248,255,255,10,2,0,64,226,53,0,80,227,129,255,255,58,236,1,159,229,128,16,141,226,54,32,160,227,119,1,0,235,224,177,159,229,5,0,160,225,113,32,160,227,11,16,160,225,231,1,0,235,0,0,80,227,118,255,255,10,200,1,159,229,0,16,144,229,5,0,160,225,49,255,47,225,0,64,160,225,1,0,112,227,68,0,0,218,176,1,159,229,4,16,160,225,0,32,160,227,170,0,0,235,128,64,141,226,10,0,160,225,4,16,160,225,113,32,160,227,1,48,160,227,39,2,0,235,70,96,160,227,0,0,80,227,35,0,0,10,128,0,221,229,72,0,80,227,32,0,0,26,1,0,160,227,113,0,80,227,7,0,0,10,0,16,212,231,0,32,219,231,1,0,128,226,2,0,81,225,248,255,255,10,2,0,64,226,112,0,80,227,21,0,0,58,72,1,159,229,128,16,141,226,113,32,160,227,74,1,0,235,0,96,160,227,15,0,0,234,40,1,159,229,0,16,144,229,5,0,160,225,49,255,47,225,0,64,160,225,0,32,160,227,1,0,112,227,3,0,0,202,48,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,4,1,159,229,4,16,160,225,126,0,0,235,70,96,160,227,52,176,157,229,0,128,160,227,0,0,90,227,110,254,255,74,216,0,159,229,0,16,144,229,10,0,160,225,49,255,47,225,0,64,160,225,1,0,112,227,4,0,0,202,48,0,157,229,0,0,144,229,48,255,47,225,0,128,144,229,70,96,160,227,184,0,159,229,4,16,160,225,8,32,160,225,51,255,255,234,48,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,140,0,159,229,226,255,255,234,16,2,70,161,36,105,8,192,17,105,32,192,40,46,0,0,4,48,0,0,44,48,0,0,59,46,0,0,73,46,0,0,48,48,0,0,108,47,0,0,115,47,0,0,8,48,0,0,91,46,0,0,128,47,0,0,136,47,0,0,107,46,0,0,12,48,0,0,130,46,0,0,16,48,0,0,135,46,0,0,146,46,0,0,20,48,0,0,160,46,0,0,167,46,0,0,24,48,0,0,181,46,0,0,28,48,0,0,189,46,0,0,32,48,0,0,204,46,0,0,211,46,0,0,96,45,0,0,224,46,0,0,151,45,0,0,36,48,0,0,239,46,0,0,254,46,0,0,14,47,0,0,29,47,0,0,42,47,0,0,72,48,0,0,55,47,0,0,73,47,0,0,0,48,0,0,240,64,45,233,12,112,141,226,0,13,45,233,4,208,77,226,0,128,160,225,0,96,224,227,6,0,136,224,1,96,134,226,1,0,208,229,0,0,80,227,1,0,0,10,1,11,86,227,248,255,255,58,0,64,160,227,0,0,86,227,28,0,0,10,140,160,159,229,140,0,159,229,0,176,160,227,0,0,141,229,0,48,154,229,4,80,70,224,4,16,136,224,1,0,160,227,5,32,160,225,51,255,47,225,1,0,80,227,3,0,0,186,5,0,80,225,1,0,0,138,4,64,128,224,7,0,0,234,1,0,112,227,10,0,0,202,0,0,157,229,0,0,144,229,48,255,47,225,0,0,144,229,4,0,80,227,4,0,0,26,14,0,91,227,2,0,0,138,1,176,139,226,4,0,86,225,230,255,255,138,6,0,84,225,28,0,159,21,0,16,144,21,1,16,129,18,0,16,128,21,24,208,71,226,0,13,189,232,240,128,189,232,40,48,0,0,44,48,0,0,72,48,0,0,144,64,45,233,4,112,141,226,100,208,77,226,0,48,160,227,3,144,208,231,1,64,131,226,0,0,89,227,2,0,0,10,1,11,83,227,4,48,160,225,248,255,255,58,1,224,68,226,70,0,94,227,38,0,0,138,13,192,160,225,4,144,140,224,1,0,84,227,5,0,0,10,0,48,160,227,3,64,208,231,3,64,204,231,1,48,131,226,3,0,94,225,250,255,255,26,32,48,160,227,1,48,73,229,108,192,159,229,1,0,137,226,28,48,160,227,15,224,160,227,49,67,14,224,4,48,67,226,4,64,220,231,4,0,115,227,1,64,64,229,1,0,128,226,248,255,255,26,32,16,160,227,8,16,201,229,28,16,160,227,15,48,160,227,50,65,3,224,4,16,65,226,4,64,220,231,4,0,113,227,1,64,192,228,249,255,255,26,0,16,160,227,1,16,192,229,10,16,160,227,0,16,192,229,13,0,160,225,146,255,255,235,4,208,71,226,144,128,189,232,91,47,0,0,240,64,45,233,12,112,141,226,4,128,45,229,40,208,77,226,1,218,77,226,7,208,205,227,156,144,159,229,156,192,159,229,1,10,160,227,36,0,141,229,8,0,141,226,118,64,156,232,0,48,160,225,118,64,131,232,0,80,153,229,36,96,141,226,40,32,141,226,6,16,160,227,6,48,160,225,0,64,160,227,0,64,141,229,4,64,141,229,53,255,47,225,0,80,160,225,1,0,112,227,3,0,0,202,84,0,159,229,0,0,144,229,48,255,47,225,0,96,160,225,72,0,159,229,0,32,150,229,5,16,160,225,165,255,255,235,0,0,85,227,6,0,0,74,36,32,157,229,1,10,82,227,3,0,0,138,40,0,159,229,40,16,141,226,88,0,0,235,1,64,160,227,4,0,160,225,16,208,71,226,4,128,157,228,240,128,189,232,52,48,0,0,16,46,0,0,44,48,0,0,143,47,0,0,156,47,0,0,240,64,45,233,12,112,141,226,0,5,45,233,8,208,77,226,12,65,159,229,3,16,160,227,0,80,160,225,0,32,148,229,50,255,47,225,0,96,160,225,1,0,112,227,43,0,0,218,244,0,159,229,6,16,160,225,0,32,160,227,0,160,160,227,130,255,255,235,0,48,148,229,4,32,134,227,5,0,160,225,4,16,160,227,4,128,160,227,51,255,47,225,0,96,160,225,1,0,112,227,35,0,0,218,192,0,159,229,6,16,160,225,0,32,160,227,117,255,255,235,180,0,159,229,1,16,160,227,34,32,160,227,1,42,130,227,0,96,144,229,4,16,141,229,255,16,160,227,255,28,129,227,4,48,141,226,5,0,160,225,0,128,141,229,54,255,47,225,0,80,160,225,1,0,112,227,3,0,0,202,108,0,159,229,0,0,144,229,48,255,47,225,0,160,144,229,108,0,159,229,5,16,160,225,10,32,160,225,94,255,255,235,21,15,111,225,160,2,160,225,13,0,0,234,64,0,159,229,0,0,144,229,48,255,47,225,0,32,144,229,52,0,159,229,4,0,0,234,40,0,159,229,0,0,144,229,48,255,47,225,0,32,144,229,32,0,159,229,6,16,160,225,78,255,255,235,0,0,160,227,20,208,71,226,0,5,189,232,240,128,189,232,56,48,0,0,44,48,0,0,168,47,0,0,178,47,0,0,60,48,0,0,188,47,0,0,240,64,45,233,12,112,141,226,0,13,45,233,104,208,77,226,0,16,141,229,2,64,160,225,2,16,160,225,0,32,160,227,0,80,160,227,58,255,255,235,0,0,84,227,39,0,0,10,164,128,159,229,32,96,160,227,4,160,141,226,0,176,160,227,68,0,160,227,4,0,205,229,2,0,160,227,4,0,91,225,5,96,205,229,21,0,0,42,0,0,157,229,0,32,160,227,11,16,128,224,2,0,160,227,2,48,209,231,130,96,138,224,2,0,128,226,35,82,216,231,15,48,3,226,30,0,82,227,3,48,216,231,2,80,198,229,1,80,130,226,3,48,198,229,11,48,133,224,2,0,0,138,5,32,160,225,4,0,83,225,240,255,255,58,3,176,160,225,0,80,160,227,32,96,160,227,10,16,160,227,0,16,202,231,1,0,128,227,0,80,202,231,10,0,160,225,218,254,255,235,4,0,91,225,219,255,255,58,24,208,71,226,0,13,189,232,240,128,189,232,91,47,0,0,240,64,45,233,12,112,141,226,0,13,45,233,12,208,77,226,204,80,159,229,0,176,160,225,200,0,159,229,20,64,160,227,4,128,141,226,113,160,255,230,0,0,141,229,0,0,160,227,186,0,205,225,184,160,205,225,0,48,149,229,8,0,160,225,1,16,160,227,100,32,160,227,4,176,141,229,51,255,47,225,0,96,160,225,1,0,112,227,2,0,0,218,0,0,86,227,6,0,0,10,11,0,0,234,0,0,157,229,0,0,144,229,48,255,47,225,0,0,144,229,4,0,80,227,13,0,0,26,1,64,84,226,232,255,255,26,96,0,159,229,11,16,160,225,10,32,160,225,13,0,0,234,186,16,221,225,0,0,160,227,32,0,17,227,11,0,0,26,24,0,138,227,0,0,17,224,1,0,160,19,7,0,0,234,0,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,28,0,159,229,6,16,160,225,215,254,255,235,0,0,160,227,24,208,71,226,0,13,189,232,240,128,189,232,64,48,0,0,44,48,0,0,198,47,0,0,209,47,0,0,240,64,45,233,12,112,141,226,0,13,45,233,12,208,77,226,2,176,160,225,0,0,82,227,8,16,141,229,52,0,0,10,0,160,160,225,20,1,159,229,0,128,160,227,0,64,160,227,4,0,141,229,8,1,159,229,0,0,141,229,8,0,157,229,4,96,75,224,6,32,160,225,4,16,128,224,4,0,157,229,0,48,144,229,10,0,160,225,51,255,47,225,0,80,160,225,1,0,80,227,3,0,0,186,6,0,85,225,1,0,0,138,4,64,133,224,25,0,0,234,0,96,160,227,1,0,117,227,32,0,0,202,11,96,160,225,10,176,160,225,0,160,157,229,0,0,154,229,48,255,47,225,0,0,144,229,4,0,80,227,2,0,0,26,11,160,160,225,6,176,160,225,11,0,0,234,0,0,154,229,48,255,47,225,0,0,144,229,35,0,80,227,18,0,0,26,11,160,160,225,10,0,160,225,4,16,160,227,6,176,160,225,140,255,255,235,0,0,80,227,11,0,0,10,62,0,88,227,1,128,136,146,11,0,84,145,210,255,255,58,0,0,0,234,0,64,160,227,11,0,68,224,16,15,111,225,160,98,160,225,9,0,0,234,0,32,160,227,4,0,0,234,0,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,0,96,160,227,28,0,159,229,5,16,160,225,131,254,255,235,6,0,160,225,24,208,71,226,0,13,189,232,240,128,189,232,40,48,0,0,44,48,0,0,221,47,0,0,240,64,45,233,12,112,141,226,0,13,45,233,20,208,77,226,4,48,141,229,12,16,141,229,0,176,160,225,20,1,159,229,2,80,160,225,0,64,160,227,8,0,141,229,8,1,159,229,64,128,160,227,0,0,141,229,12,0,157,229,4,160,85,224,19,16,141,226,4,16,128,128,8,0,157,229,1,32,160,227,10,32,160,129,0,48,144,229,11,0,160,225,51,255,47,225,0,96,160,225,1,0,80,227,7,0,0,186,10,0,86,225,44,0,0,138,4,0,157,229,4,64,134,224,0,0,80,227,5,0,84,1,19,0,0,26,21,0,0,234,0,0,86,227,21,0,0,10,0,0,157,229,0,0,144,229,48,255,47,225,0,0,144,229,4,0,80,227,10,0,0,10,0,0,157,229,0,0,144,229,48,255,47,225,0,0,144,229,35,0,80,227,17,0,0,26,11,0,160,225,1,16,160,227,59,255,255,235,0,0,80,227,12,0,0,10,1,128,88,226,213,255,255,26,16,0,0,234,1,0,160,227,15,0,0,234,4,16,157,229,5,0,68,224,16,15,111,225,0,0,81,227,160,2,160,225,1,16,160,19,0,0,1,224,7,0,0,234,0,0,157,229,0,0,144,229,48,255,47,225,0,32,144,229,28,0,159,229,6,16,160,225,50,254,255,235,0,0,160,227,24,208,71,226,0,13,189,232,240,128,189,232,68,48,0,0,44,48,0,0,232,47,0,0};
static void own_serial_byte(unsigned char c){if(c=='\r')return;if(c=='\n'){own_line[own_line_n]=0;if(own_seen&&!strcmp(own_line,"LOOPBACK_PASS_V1"))own_serial_pass=true;own_line_n=0;}else if(own_line_n+1<sizeof own_line)own_line[own_line_n++]=c;else own_line_n=0;}

typedef struct { FILE *serial; const s5l8900_t *m; bool bol; } tap_t;
/* Serial output; each line in the --serial file is prefixed with the guest
 * instruction count so kernel messages can be correlated with --swi-log. */
static void tap(void *ctx,uint8_t c) {
    tap_t *t=ctx; own_serial_byte(c); putchar((int)c); fflush(stdout);
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
static bool own_read(s5l8900_t*m,uint32_t address,void*dst,unsigned n){unsigned char*p=dst;if(n&&address>UINT32_MAX-(n-1))return false;for(unsigned i=0;i<n;i++){uint32_t a=address+i,w;if(!read_va32(m,a&~3u,&w))return false;p[i]=(unsigned char)(w>>(8*(a&3u)));}return true;}
static bool own_string(s5l8900_t*m,uint32_t address,char*s,unsigned n){for(unsigned i=0;i<n;i++){if(!own_read(m,address+i,s+i,1))return false;if(!s[i])return true;}return false;}
static bool watch_hook(void *ctx){(void)ctx;s5l8900_t*m=(s5l8900_t*)g_watch_m;uint32_t pc=m->cpu.r[15];if((m->cpu.cpsr&0x3fu)!=0x10u)return false;
 if(pc==0x1a60u){unsigned char bytes[sizeof own_text];if(!own_read(m,0x1a60u,bytes,sizeof bytes)||memcmp(bytes,own_text,sizeof bytes))return false;own_seen=true;own_ttbr=m->cpu.cp15.ttbr0;own_entries++;if(own_entries!=1)own_failed=true;printf("OWN entry cycle=%llu pc=%08x cpsr=%08x ttbr=%08x exact_text=%zu\n",(unsigned long long)m->cpu.cycles,pc,m->cpu.cpsr,own_ttbr,sizeof bytes);}
 if(!own_seen||m->cpu.cp15.ttbr0!=own_ttbr)return false;
 printf("OWN witness cycle=%llu pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",(unsigned long long)m->cpu.cycles,pc,m->cpu.r[0],m->cpu.r[1],m->cpu.r[2],m->cpu.r[3],m->cpu.r[14]);
 if(pc==0x2600u){char label[80];if(!own_string(m,m->cpu.r[0],label,sizeof label)){own_failed=true;}else printf("OWN result label=%s value=%08x error_or_detail=%08x\n",label,m->cpu.r[1],m->cpu.r[2]);}
 if(pc==0x28ecu){char label[80];unsigned char bytes[4096];unsigned n=m->cpu.r[2];if(n>sizeof bytes||!own_string(m,m->cpu.r[0],label,sizeof label)||!own_read(m,m->cpu.r[1],bytes,n)){own_failed=true;}else{printf("OWN bytes label=%s length=%u hex=",label,n);for(unsigned i=0;i<n;i++)printf("%02x",bytes[i]);puts("");}}
 if(pc==0x30009860u&&m->cpu.r[0]==1){static const char terminal[]="LOOPBACK_PASS_V1\n";char data[sizeof terminal];if(m->cpu.r[2]==sizeof terminal-1&&own_read(m,m->cpu.r[1],data,sizeof terminal-1)&&!memcmp(data,terminal,sizeof terminal-1))own_pass_written=true;}
 if(pc==0x300099f0u){own_exit_seen=true;own_exit_status=m->cpu.r[0];if(own_exit_status)own_failed=true;printf("OWN exit_entry status=%u\n",own_exit_status);}
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
 if(own_failed){reason="own-loopback-observer-or-guest-failure";break;}
 if(own_exit_seen){reason=(own_pass_written&&own_serial_pass)?"own-loopback-qualified":"own-loopback-missing-proof";break;}

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
 printf("OWN final seen=%d entries=%u pass_written=%d serial_pass=%d exit_seen=%d exit_status=%u failed=%d ttbr=%08x\n",own_seen,own_entries,own_pass_written,own_serial_pass,own_exit_seen,own_exit_status,own_failed,own_ttbr);
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
    if (shot&&!save_ppm(shot,&m)) fprintf(stderr,"screenshot failed: %s\n",shot);
    if (dump_ram) {
        FILE *rf=fopen(dump_ram,"wb");
        if (!rf||fwrite(m.ram,1,m.ram_size,rf)!=m.ram_size) fprintf(stderr,"ram dump failed: %s\n",dump_ram);
        if (rf) fclose(rf);
    }
    if (t.serial) fclose(t.serial); s5l8900_free(&m); free(k.p);free(d.p);free(rd.p); return 0;
}
