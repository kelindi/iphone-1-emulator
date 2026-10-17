/*
 * One-shot restore step: copy the booted RAM-disk root (/dev/rmd0) onto the
 * NAND block device (/dev/rdisk0) through the original FTL, then grow the
 * HFSX volume on disk0 to TARGET_BYTES. The allocation bitmap already covers
 * the target size, so growing only touches the bitmap, the primary and the
 * alternate volume headers. Only ordinary original libSystem calls are used.
 */
_Static_assert(sizeof(void *) == 4, "OS1 ARM32 ABI");
typedef unsigned int u32;
typedef unsigned char u8;
#define IMPORT(result,name,args) extern result name args; static result (*volatile api_##name) args = name
IMPORT(int,open,(const char *,int,...));
IMPORT(int,read,(int,void *,u32));
IMPORT(int,write,(int,const void *,u32));
IMPORT(int,close,(int));
IMPORT(int,fsync,(int));
IMPORT(void,sync,(void));
IMPORT(long long,lseek,(int,long long,int));
IMPORT(void,_exit,(int));

#define O_RDONLY 0
#define O_RDWR   2
#define BLOCK    4096u
#define CHUNK    (128u * 1024u)
#define TARGET_BYTES (384ull * 1024u * 1024u)
#define HFS_UNMOUNTED   0x100u
#define HFS_INCONSISTENT 0x800u

static u8 buf[CHUNK];

static void say(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    api_write(1, s, n);
}

static void say_hex(const char *tag, u32 v) {
    char out[24];
    u32 at = 0;
    while (tag[at] && at < 12) { out[at] = tag[at]; at++; }
    for (int i = 7; i >= 0; i--) out[at++] = "0123456789abcdef"[(v >> (i * 4)) & 15];
    out[at++] = '\n';
    out[at] = 0;
    say(out);
}

static u32 be32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static void put32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

static int fail(const char *why) {
    say("PROVISION_FAILED ");
    say(why);
    say("\n");
    api__exit(1);
    return 1;
}

static int full_read(int fd, u8 *p, u32 n) {
    u32 off = 0;
    for (unsigned tries = 0; off < n && tries < 64; tries++) {
        int r = api_read(fd, p + off, n - off);
        if (r <= 0) return 0;
        off += (u32)r;
    }
    return off == n;
}

static int full_write(int fd, const u8 *p, u32 n) {
    u32 off = 0;
    for (unsigned tries = 0; off < n && tries < 64; tries++) {
        int r = api_write(fd, p + off, n - off);
        if (r <= 0) return 0;
        off += (u32)r;
    }
    return off == n;
}

static int at(int fd, unsigned long long off) {
    return api_lseek(fd, (long long)off, 0) == (long long)off;
}

void start(void) {
    say("PROVISION_BEGIN_V1\n");
    int dst = api_open("/dev/rdisk0", O_RDWR);
    if (dst < 0) fail("open rdisk0");
    if (!full_read(dst, buf, BLOCK)) fail("read disk0 header");
    if (buf[1024] == 'H' && buf[1025] == 'X' &&
        (unsigned long long)be32(buf + 1024 + 44) * BLOCK == TARGET_BYTES) {
        say("PROVISION_ALREADY_V1\n");
        api__exit(0);
    }
    int src = api_open("/dev/rmd0", O_RDONLY);
    if (src < 0) fail("open rmd0");
    api_sync();
    if (!full_read(src, buf, BLOCK)) fail("read md0 header");
    const u8 *vh = buf + 1024;
    if (vh[0] != 'H' || vh[1] != 'X' || be32(vh + 40) != BLOCK) fail("md0 is not 4K HFSX");
    u32 old_total = be32(vh + 44);
    u32 new_total = (u32)(TARGET_BYTES / BLOCK);
    u32 bitmap_start = be32(vh + 112 + 16);
    u32 bitmap_bytes = be32(vh + 112 + 4);   /* low word of logicalSize */
    if (new_total <= old_total || bitmap_bytes * 8u < new_total || bitmap_bytes > CHUNK)
        fail("bitmap does not cover target");
    say_hex("old_total ", old_total);

    if (!at(src, 0) || !at(dst, 0)) fail("seek");
    unsigned long long bytes = (unsigned long long)old_total * BLOCK;
    for (unsigned long long done = 0; done < bytes; done += CHUNK) {
        u32 n = bytes - done < CHUNK ? (u32)(bytes - done) : CHUNK;
        if (!full_read(src, buf, n)) fail("read md0");
        if (!full_write(dst, buf, n)) fail("write disk0");
        if ((done & 0xffffffull) == 0) say_hex("copied ", (u32)(done >> 20));
    }
    api_close(src);

    /* Bitmap: free the old alternate-header block and everything after it,
     * then reserve the new last block for the new alternate header. */
    if (!at(dst, (unsigned long long)bitmap_start * BLOCK) || !full_read(dst, buf, bitmap_bytes))
        fail("read bitmap");
    for (u32 b = old_total - 1u; b < new_total; b++)
        buf[b / 8u] &= (u8)~(0x80u >> (b % 8u));
    buf[(new_total - 1u) / 8u] |= (u8)(0x80u >> ((new_total - 1u) % 8u));
    if (!at(dst, (unsigned long long)bitmap_start * BLOCK) || !full_write(dst, buf, bitmap_bytes))
        fail("write bitmap");

    if (!at(dst, 0) || !full_read(dst, buf, BLOCK)) fail("reread header");
    u8 *h = buf + 1024;
    put32(h + 44, new_total);
    put32(h + 48, be32(h + 48) + (new_total - old_total));
    put32(h + 4, (be32(h + 4) | HFS_UNMOUNTED) & ~HFS_INCONSISTENT);
    if (!at(dst, 0) || !full_write(dst, buf, BLOCK)) fail("write header");
    /* Alternate header: 1024 bytes before the end of the volume. */
    for (u32 i = 0; i < 512; i++) buf[BLOCK - 1024 + i] = h[i];
    for (u32 i = 0; i < BLOCK - 1024; i++) buf[i] = 0;
    for (u32 i = BLOCK - 512; i < BLOCK; i++) buf[i] = 0;
    if (!at(dst, TARGET_BYTES - BLOCK) || !full_write(dst, buf, BLOCK)) fail("write alt header");
    api_fsync(dst);
    api_close(dst);
    api_sync();
    say_hex("new_total ", new_total);
    say("PROVISION_COMPLETE_V1\n");
    api__exit(0);
}
