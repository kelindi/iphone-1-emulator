#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "classifier.inc"

static uint8_t original(const uint8_t *page, size_t len) {
    uint8_t first = page[0], cls = 1;
    for (size_t i = 1; i < len; ++i)
        if (page[i] != first) { cls = 2; break; }
    if (cls == 1 && first == 0) cls = 0;
    return cls;
}
static unsigned checks;
static void check(const uint8_t *page, size_t len) {
    ++checks;
    if (snap_page_class(page,len) != original(page,len)) {
        fprintf(stderr,"mismatch check=%u length=%zu\n",checks,len); exit(1);
    }
}
int main(void) {
    uint8_t storage[4096+8];
    for (unsigned align=0;align<8;++align) {
        uint8_t *page=storage+align;
        for (size_t len=1;len<=4096;++len) {
            memset(page,(uint8_t)len,len); check(page,len);
            if(len>1) { page[len-1]^=1; check(page,len); }
        }
    }
    for (unsigned value=0;value<256;++value) {
        memset(storage,value,4096); check(storage,4096);
        // Every byte value and every possible position of the first difference.
        for(size_t pos=0;pos<4096;++pos) {
            storage[pos]^=1; check(storage,4096); storage[pos]^=1;
        }
    }
    uint32_t random=0x41289000;
    for (unsigned trial=0;trial<10000;++trial) {
        for(size_t i=0;i<4096;++i) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            storage[i]=(uint8_t)random;
        }
        check(storage,1+(random%4096));
    }
    // A protected following host page catches accidental overreads of tails.
    const size_t host_page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *guard=mmap(NULL,host_page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(guard==MAP_FAILED || mprotect(guard+host_page,host_page,PROT_NONE))return 2;
    for(size_t len=1;len<=4096 && len<=host_page;++len) {
        uint8_t *page=guard+host_page-len;
        memset(page,0,len);check(page,len);
        memset(page,0xff,len);check(page,len);
        page[len-1]=0;check(page,len);
    }
    munmap(guard,host_page*2);
    printf("PASS %u page classification comparisons; all bytes/first-difference positions, lengths/alignments and guarded tails\n",checks);
    return 0;
}
