#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
__attribute__((noinline,noipa)) unsigned old(uint32_t insn){
if (( insn & 0x0c000000u ) == 0x00000000u && ( insn & 0x01900000u ) != 0x01000000u && ! ( ( insn & 0x0e000000u ) == 0x00000000u && ( insn & 0x00000090u ) == 0x00000090u )) return 0;
if (( insn & 0x0f000000u ) == 0x0a000000u || ( insn & 0x0f000000u ) == 0x0b000000u) return 1;
if (( insn & 0x0fc000f0u ) == 0x00000090u) return 2;
if (( insn & 0x0f8000f0u ) == 0x00800090u) return 3;
if (( insn & 0x0f900090u ) == 0x01000080u) return 4;
if (( insn & 0x0e000000u ) == 0x06000000u && ( insn & 0x00000010u ) != 0) return 5;
if (( insn & 0x0c000000u ) == 0x04000000u) return 6;
if (( insn & 0x0e000000u ) == 0x08000000u) return 7;
if (( insn & 0x0e000000u ) == 0x00000000u && ( insn & 0x00000090u ) == 0x00000090u && ( insn & 0x00000060u ) != 0x00000000u) return 8;
if (( insn & 0x0ff00ff0u ) == 0x01900f90u) return 9;
if (( insn & 0x0ff00ff0u ) == 0x01800f90u) return 10;
if (( insn & 0x0ff00ff0u ) == 0x01b00f90u) return 11;
if (( insn & 0x0ff00ff0u ) == 0x01a00f90u) return 12;
if (( insn & 0x0ff00ff0u ) == 0x01d00f90u) return 13;
if (( insn & 0x0ff00ff0u ) == 0x01c00f90u) return 14;
if (( insn & 0x0ff00ff0u ) == 0x01f00f90u) return 15;
if (( insn & 0x0ff00ff0u ) == 0x01e00f90u) return 16;
if (( insn & 0x0fb00ff0u ) == 0x01000090u) return 17;
if (( insn & 0x0e000000u ) == 0x00000000u && ( insn & 0x00000090u ) == 0x00000090u) return 18;
if (insn == 0xe320f003u) return 19;
if (( insn & 0x0f000010u ) == 0x0e000010u) return 20;
if (( insn & 0x0f000e10u ) == 0x0e000a00u || ( insn & 0x0e000e00u ) == 0x0c000a00u) return 21;
if (( insn & 0x0f000000u ) == 0x0f000000u) return 22;
if (( insn & 0x0c000000u ) == 0x00000000u) return 23;
return 24;}
__attribute__((noinline,noipa)) unsigned candidate(uint32_t insn){switch((insn>>26)&3u){
case 0:
if (( insn & 0x0c000000u ) == 0x00000000u && ( insn & 0x01900000u ) != 0x01000000u && ! ( ( insn & 0x0e000000u ) == 0x00000000u && ( insn & 0x00000090u ) == 0x00000090u )) return 0;
if (( insn & 0x0fc000f0u ) == 0x00000090u) return 2;
if (( insn & 0x0f8000f0u ) == 0x00800090u) return 3;
if (( insn & 0x0f900090u ) == 0x01000080u) return 4;
if (( insn & 0x0e000000u ) == 0x00000000u && ( insn & 0x00000090u ) == 0x00000090u && ( insn & 0x00000060u ) != 0x00000000u) return 8;
if (( insn & 0x0ff00ff0u ) == 0x01900f90u) return 9;
if (( insn & 0x0ff00ff0u ) == 0x01800f90u) return 10;
if (( insn & 0x0ff00ff0u ) == 0x01b00f90u) return 11;
if (( insn & 0x0ff00ff0u ) == 0x01a00f90u) return 12;
if (( insn & 0x0ff00ff0u ) == 0x01d00f90u) return 13;
if (( insn & 0x0ff00ff0u ) == 0x01c00f90u) return 14;
if (( insn & 0x0ff00ff0u ) == 0x01f00f90u) return 15;
if (( insn & 0x0ff00ff0u ) == 0x01e00f90u) return 16;
if (( insn & 0x0fb00ff0u ) == 0x01000090u) return 17;
if (( insn & 0x0e000000u ) == 0x00000000u && ( insn & 0x00000090u ) == 0x00000090u) return 18;
if (insn == 0xe320f003u) return 19;
if (( insn & 0x0c000000u ) == 0x00000000u) return 23;
return 24;
case 1:
if (( insn & 0x0e000000u ) == 0x06000000u && ( insn & 0x00000010u ) != 0) return 5;
if (( insn & 0x0c000000u ) == 0x04000000u) return 6;
return 24;
case 2:
if (( insn & 0x0f000000u ) == 0x0a000000u || ( insn & 0x0f000000u ) == 0x0b000000u) return 1;
if (( insn & 0x0e000000u ) == 0x08000000u) return 7;
return 24;
case 3:
if (( insn & 0x0f000010u ) == 0x0e000010u) return 20;
if (( insn & 0x0f000e10u ) == 0x0e000a00u || ( insn & 0x0e000e00u ) == 0x0c000a00u) return 21;
if (( insn & 0x0f000000u ) == 0x0f000000u) return 22;
return 24;
}return 24;}
static unsigned checks;void check(uint32_t i){checks++;if(old(i)!=candidate(i)){printf("FAIL %08x\n",i);exit(1);}}
int main(void){unsigned bits[16]={4,5,6,7,8,9,10,11,20,21,22,23,24,25,26,27};for(unsigned x=0;x<65536;x++){uint32_t i=0;for(unsigned b=0;b<16;b++)i|=((x>>b)&1u)<<bits[b];for(unsigned c=0;c<16;c++){check(i|(c<<28));check(i|(c<<28)|0xff00fu);}}check(0xe320f003u);for(unsigned b=0;b<32;b++)check(0xe320f003u^(1u<<b));printf("PASS %u independent selections\n",checks);}
