/* Only ordinary, eagerly bound original libSystem calls. No Apple code edits. */
#include "import.h"
_Static_assert(sizeof(void *) == 4 && sizeof(photo_u32) == 4, "OS1 ARM32 ABI");
#define IMPORT(result,name,args) extern result name args; static result (*volatile api_##name) args = name
IMPORT(int,open,(const char *,int,...));
IMPORT(int,read,(int,void *,photo_u32));
IMPORT(int,write,(int,const void *,photo_u32));
IMPORT(int,close,(int));
IMPORT(int,mkdir,(const char *,unsigned short));
IMPORT(int,fchdir,(int));
IMPORT(int,fsync,(int));
IMPORT(int,link,(const char *,const char *));
IMPORT(int,unlink,(const char *));
IMPORT(photo_u32,getuid,(void));
IMPORT(int *,__error,(void));
IMPORT(void,_exit,(int));
#include "assets.h"

static void line(const char *s, photo_u32 size) {
    photo_u32 off = 0;
    for (unsigned calls = 0; calls < 32 && off < size; calls++) {
        int n = api_write(1, s + off, size - off);
        if (n < 0 && *api___error() == 4) continue;
        if (n <= 0 || (photo_u32)n > size - off) break;
        off += (photo_u32)n;
    }
}
static __attribute__((noinline)) void receipt(photo_u32 index, int status,
                                              photo_u32 bytes, photo_u32 digest) {
    char out[64] = "PHOTO_FILE_V1 ";
    photo_u32 values[4] = {index, (photo_u32)status, bytes, digest};
    unsigned at = 14;
    for (unsigned j = 0; j < 4; j++) {
        for (int i = 7; i >= 0; i--) out[at++] = "0123456789abcdef"[(values[j] >> (i * 4)) & 15];
        out[at++] = j == 3 ? '\n' : ' ';
    }
    line(out, at);
}
void start(void) {
    const struct photo_api a = {api_open, api_read, api_write, api_close, api_mkdir,
        api_fchdir, api_fsync, api_link, api_unlink, api_getuid, api___error, receipt};
    line("PHOTO_BEGIN_V1\n", 15);
    int result = photo_import(&a, "/private/var/root", photo_assets, 5);
    if (result == 0) line("PHOTO_COMPLETE_V1\n", 18);
    else line("PHOTO_FAILED_V1\n", 16);
    api__exit(result);
}
