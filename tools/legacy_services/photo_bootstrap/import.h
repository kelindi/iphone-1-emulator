/* Independently authored, fixed DCF test-data importer. Darwin OS1 ABI values. */
#ifndef IPHONE1_PHOTO_IMPORT_H
#define IPHONE1_PHOTO_IMPORT_H
typedef unsigned int photo_u32;
struct photo_asset { const char *name, *temporary; const unsigned char *bytes; photo_u32 size; };
struct photo_api {
    int (*open)(const char *, int, ...);
    int (*read)(int, void *, photo_u32);
    int (*write)(int, const void *, photo_u32);
    int (*close)(int);
    int (*mkdir)(const char *, unsigned short);
    int (*fchdir)(int);
    int (*fsync)(int);
    int (*link)(const char *, const char *);
    int (*unlink)(const char *);
    photo_u32 (*getuid)(void);
    int *(*error)(void);
    void (*receipt)(photo_u32 index, int status, photo_u32 bytes, photo_u32 digest);
};
enum { PHOTO_RDONLY=0, PHOTO_WRONLY=1, PHOTO_NONBLOCK=4, PHOTO_NOFOLLOW=0x100,
       PHOTO_CREAT=0x200, PHOTO_EXCL=0x800 };
/* The root path is fixed by the guest wrapper; tests supply their private root.
 * Returns 0 only after exact readback of every asset. Never overwrites a file.
 * Leaves cwd in the target directory. This one-shot process exits immediately.
 */
int photo_import(const struct photo_api *, const char *root,
                 const struct photo_asset *, photo_u32 count);
#endif
