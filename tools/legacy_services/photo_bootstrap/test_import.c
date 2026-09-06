#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "import.h"
#include "assets.h"

/* Real disposable Linux filesystem, translated Darwin flags and finite fault
 * injection. Only the guest wrapper supplies actual original libSystem calls.
 */
enum { NONE, SHORT_IO, INTERRUPTED, ZERO_WRITE, SYNC_FAIL, CLOSE_FAIL,
       FOREIGN_RACE, SAME_RACE, UNLINK_FAIL, READ_FAIL, WRITE_FAIL };
static int fault, write_count, read_count, close_count, receipts;
static photo_u32 uid;
static int io_error;
static int *host_error(void) { return &io_error; }
static int capture(int n) { io_error = errno; return n; }
static int host_open(const char *p, int flags, ...) {
    int f = flags & PHOTO_WRONLY ? O_WRONLY : O_RDONLY;
    if (flags & PHOTO_NOFOLLOW) f |= O_NOFOLLOW;
    if (flags & PHOTO_NONBLOCK) f |= O_NONBLOCK;
    if (flags & PHOTO_CREAT) f |= O_CREAT;
    if (flags & PHOTO_EXCL) f |= O_EXCL;
    return capture(open(p, f, 0644));
}
static int host_read(int fd, void *p, photo_u32 n) {
    read_count++;
    if (fault == READ_FAIL) { io_error=EIO; return -1; }
    if (fault == INTERRUPTED && read_count % 3 == 1) { io_error=EINTR; return -1; }
    if (fault == SHORT_IO && n > 71) n = 71;
    return capture((int)read(fd, p, n));
}
static int host_write(int fd, const void *p, photo_u32 n) {
    write_count++;
    if (fault == ZERO_WRITE) return 0;
    if (fault == WRITE_FAIL) { io_error=EIO; return -1; }
    if (fault == INTERRUPTED && write_count % 3 == 1) { io_error=EINTR; return -1; }
    if (fault == SHORT_IO && n > 83) n = 83;
    return capture((int)write(fd, p, n));
}
static int host_close(int fd) {
    close_count++;
    int r=close(fd);
    if (fault == CLOSE_FAIL && close_count == 5) { io_error=EIO; return -1; }
    return capture(r);
}
static int host_mkdir(const char *p, unsigned short mode) { return capture(mkdir(p,mode)); }
static int host_chdir(int fd) { return capture(fchdir(fd)); }
static int host_sync(int fd) {
    if (fault == SYNC_FAIL) { io_error=EIO; return -1; }
    return capture(fsync(fd));
}
static int host_link(const char *p, const char *q) {
    if (fault == FOREIGN_RACE || fault == SAME_RACE) {
        int fd=open(q,O_WRONLY|O_CREAT|O_EXCL,0644); assert(fd>=0);
        if (fault == FOREIGN_RACE) assert(write(fd,"foreign",7)==7);
        else {
            unsigned i=0; while(i<5 && strcmp(q,photo_assets[i].name))i++;
            assert(i<5); assert(write(fd,photo_assets[i].bytes,photo_assets[i].size)==photo_assets[i].size);
        }
        assert(close(fd)==0);
    }
    return capture(link(p,q));
}
static int host_unlink(const char *p) {
    if (fault == UNLINK_FAIL) { io_error=EIO; return -1; }
    return capture(unlink(p));
}
static photo_u32 host_uid(void) { return uid; }
static void host_receipt(photo_u32 i,int status,photo_u32 bytes,photo_u32 digest) {
    if(status>=0) {
        photo_u32 expected=2166136261U;
        for(photo_u32 j=0;j<photo_assets[i].size;j++)expected=(expected^photo_assets[i].bytes[j])*16777619U;
        assert(bytes==photo_assets[i].size && digest==expected); receipts++;
    }
}
static const struct photo_api api={host_open,host_read,host_write,host_close,
    host_mkdir,host_chdir,host_sync,host_link,host_unlink,host_uid,host_error,host_receipt};
static void put(const char *p,const void *bytes,size_t n) {
    int fd=open(p,O_WRONLY|O_CREAT|O_EXCL,0644);assert(fd>=0);
    assert(write(fd,bytes,n)==(ssize_t)n);assert(close(fd)==0);
}
int main(int argc,char **argv) {
    assert(argc==3); const char *root=argv[1],*mode=argv[2];
    assert(chdir(root)==0);
    if(!strcmp(mode,"uid"))uid=501;
    if(!strcmp(mode,"dir-symlink"))assert(symlink("/tmp","Media")==0);
    if(!strcmp(mode,"dir-file"))put("Media","foreign",7);
    int existing=!strcmp(mode,"foreign")||!strcmp(mode,"temp")||
        !strcmp(mode,"file-symlink")||!strcmp(mode,"fifo")||!strcmp(mode,"extra-byte");
    if(existing) {
        assert(mkdir("Media",0755)==0 && chdir("Media")==0);
        assert(mkdir("DCIM",0755)==0 && chdir("DCIM")==0);
        assert(mkdir("100APPLE",0755)==0 && chdir("100APPLE")==0);
        if(!strcmp(mode,"foreign"))put(photo_assets[0].name,"foreign",7);
        if(!strcmp(mode,"temp"))put(photo_assets[0].temporary,"foreign",7);
        if(!strcmp(mode,"file-symlink"))assert(symlink("/dev/null",photo_assets[0].name)==0);
        if(!strcmp(mode,"fifo"))assert(mkfifo(photo_assets[0].name,0644)==0);
        if(!strcmp(mode,"extra-byte")) {
            put(photo_assets[0].name,photo_assets[0].bytes,photo_assets[0].size);
            int fd=open(photo_assets[0].name,O_WRONLY|O_APPEND);assert(fd>=0 && write(fd,"x",1)==1 && close(fd)==0);
        }
    }
    const char *fault_names[]={"fresh","short","eintr","zero","sync","close",
        "race","same-race","unlink","read","write"};
    for(unsigned i=0;i<sizeof fault_names/sizeof *fault_names;i++)if(!strcmp(mode,fault_names[i]))fault=(int)i;
    int result=photo_import(&api,root,photo_assets,5);
    int success=!strcmp(mode,"fresh")||!strcmp(mode,"short")||!strcmp(mode,"eintr")||!strcmp(mode,"same-race");
    assert((result==0)==success);
    if(success) {
        assert(receipts==5);
        struct stat before[5],after;
        for(unsigned i=0;i<5;i++)assert(stat(photo_assets[i].name,&before[i])==0);
        fault=NONE; receipts=0;
        assert(photo_import(&api,root,photo_assets,5)==0 && receipts==5);
        for(unsigned i=0;i<5;i++) {
            assert(stat(photo_assets[i].name,&after)==0 && before[i].st_ino==after.st_ino);
            assert(access(photo_assets[i].temporary,F_OK)<0);
        }
    }
    printf("PASS %s result=%d reads=%d writes=%d closes=%d\n",mode,result,read_count,write_count,close_count);
    return 0;
}
