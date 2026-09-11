#include "arm_code_versions.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t ram[3073] = {0};
    arm_code_versions_t v = {0};
    assert(!arm_code_versions_init(&v, ram, 0));
    assert(arm_code_versions_init(&v, ram, sizeof ram));
    assert(!arm_code_versions_init(&v, ram, sizeof ram));
    assert(v.count == 4);
    for (unsigned i=0; i<sizeof ram; i++) assert(arm_code_version(&v,ram+i)==1);
    assert(arm_code_versions_written(&v, ram+1023, 2));
    assert(arm_code_version(&v, ram)==2);
    assert(arm_code_version(&v, ram+1024)==2);
    assert(arm_code_version(&v, ram+2048)==1);
    assert(arm_code_versions_written(&v, ram+1024, 1024));
    assert(arm_code_version(&v,ram+1023)==2);
    assert(arm_code_version(&v,ram+1024)==3);
    assert(arm_code_version(&v,ram+2047)==3); /* same physical alias identity */
    assert(arm_code_version(&v,ram+2048)==1);
    assert(arm_code_versions_written(&v,NULL,0));
    assert(arm_code_versions_written(&v,ram+3072,1));
    assert(arm_code_version(&v,ram+3072)==2);
    v.versions[1]=UINT32_MAX;
    uint32_t before=v.versions[0];
    assert(!arm_code_versions_written(&v,ram+1023,2));
    assert(v.exhausted && v.versions[0]==before); /* never wrap or half-advance */
    assert(!arm_code_version(&v,ram));
    arm_code_versions_clear(&v);
    assert(!v.ram&&!v.versions&&!v.exhausted);
    assert(arm_code_versions_init(&v,ram,sizeof ram));
    assert(!arm_code_versions_written(&v,ram+3072,2));
    assert(v.exhausted&&!arm_code_version(&v,ram));
    arm_code_versions_clear(&v);
    assert(arm_code_versions_init(&v,ram,sizeof ram));
    assert(!arm_code_versions_written(&v,(const uint8_t *)(uintptr_t)1,1));
    arm_code_versions_clear(&v);
    puts("Physical code versions: ranges, aliases, partial last granule, invalid spans and exhaustion PASS");
    return 0;
}
