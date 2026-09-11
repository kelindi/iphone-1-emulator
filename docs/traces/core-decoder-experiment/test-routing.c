#include <stdint.h>
#include <stdio.h>
static int original(uint32_t insn){if ((insn & 0x0c000000u) == 0x00000000u &&        /* the DP encoding space */
        (insn & 0x01900000u) != 0x01000000u &&        /* not misc: MSR/BX/CLZ/DSP */
        !((insn & 0x0e000000u) == 0x00000000u &&
          (insn & 0x00000090u) == 0x00000090u)) return 0;
else if ((insn & 0x0f000000u) == 0x0a000000u ||
        (insn & 0x0f000000u) == 0x0b000000u) return 1;
else if ((insn & 0x0fc000f0u) == 0x00000090u) return 2;
else if ((insn & 0x0f8000f0u) == 0x00800090u) return 3;
else if ((insn & 0x0f900090u) == 0x01000080u) return 4;
else if ((insn & 0x0e000000u) == 0x06000000u &&
               (insn & 0x00000010u) != 0) return 5;
else if ((insn & 0x0c000000u) == 0x04000000u) return 6;
else if ((insn & 0x0e000000u) == 0x08000000u) return 7;
else if ((insn & 0x0e000000u) == 0x00000000u &&
               (insn & 0x00000090u) == 0x00000090u &&
               (insn & 0x00000060u) != 0x00000000u) return 8;
else if ((insn & 0x0ff00ff0u) == 0x01900f90u) return 9;
else if ((insn & 0x0ff00ff0u) == 0x01800f90u) return 10;
else if ((insn & 0x0ff00ff0u) == 0x01b00f90u) return 11;
else if ((insn & 0x0ff00ff0u) == 0x01a00f90u) return 12;
else if ((insn & 0x0ff00ff0u) == 0x01d00f90u) return 13;
else if ((insn & 0x0ff00ff0u) == 0x01c00f90u) return 14;
else if ((insn & 0x0ff00ff0u) == 0x01f00f90u) return 15;
else if ((insn & 0x0ff00ff0u) == 0x01e00f90u) return 16;
else if ((insn & 0x0fb00ff0u) == 0x01000090u) return 17;
else if ((insn & 0x0e000000u) == 0x00000000u &&
               (insn & 0x00000090u) == 0x00000090u) return 18;
else if (insn == 0xe320f003u) return 19;
else if ((insn & 0x0f000010u) == 0x0e000010u) return 20;
else if ((insn & 0x0f000e10u) == 0x0e000a00u ||     /* VFP CDP          */
               (insn & 0x0e000e00u) == 0x0c000a00u) return 21;
else if ((insn & 0x0f000000u) == 0x0f000000u) return 22;
else if ((insn & 0x0c000000u) == 0x00000000u) return 23;
return -1;
}
static int candidate(uint32_t insn){switch((insn>>25)&7){case 0: if ((insn & 0x0c000000u) == 0x00000000u &&        /* the DP encoding space */
        (insn & 0x01900000u) != 0x01000000u &&        /* not misc: MSR/BX/CLZ/DSP */
        !((insn & 0x0e000000u) == 0x00000000u &&
          (insn & 0x00000090u) == 0x00000090u)) return 0;
else if ((insn & 0x0fc000f0u) == 0x00000090u) return 2;
else if ((insn & 0x0f8000f0u) == 0x00800090u) return 3;
else if ((insn & 0x0f900090u) == 0x01000080u) return 4;
else if ((insn & 0x0e000000u) == 0x00000000u &&
               (insn & 0x00000090u) == 0x00000090u &&
               (insn & 0x00000060u) != 0x00000000u) return 8;
else if ((insn & 0x0ff00ff0u) == 0x01900f90u) return 9;
else if ((insn & 0x0ff00ff0u) == 0x01800f90u) return 10;
else if ((insn & 0x0ff00ff0u) == 0x01b00f90u) return 11;
else if ((insn & 0x0ff00ff0u) == 0x01a00f90u) return 12;
else if ((insn & 0x0ff00ff0u) == 0x01d00f90u) return 13;
else if ((insn & 0x0ff00ff0u) == 0x01c00f90u) return 14;
else if ((insn & 0x0ff00ff0u) == 0x01f00f90u) return 15;
else if ((insn & 0x0ff00ff0u) == 0x01e00f90u) return 16;
else if ((insn & 0x0fb00ff0u) == 0x01000090u) return 17;
else if ((insn & 0x0e000000u) == 0x00000000u &&
               (insn & 0x00000090u) == 0x00000090u) return 18;
else if ((insn & 0x0c000000u) == 0x00000000u) return 23;
return -1;
case 1: if ((insn & 0x0c000000u) == 0x00000000u &&        /* the DP encoding space */
        (insn & 0x01900000u) != 0x01000000u &&        /* not misc: MSR/BX/CLZ/DSP */
        !((insn & 0x0e000000u) == 0x00000000u &&
          (insn & 0x00000090u) == 0x00000090u)) return 0;
else if (insn == 0xe320f003u) return 19;
else if ((insn & 0x0c000000u) == 0x00000000u) return 23;
return -1;
case 2: if ((insn & 0x0c000000u) == 0x04000000u) return 6;
return -1;
case 3: if ((insn & 0x0e000000u) == 0x06000000u &&
               (insn & 0x00000010u) != 0) return 5;
else if ((insn & 0x0c000000u) == 0x04000000u) return 6;
return -1;
case 4: if ((insn & 0x0e000000u) == 0x08000000u) return 7;
return -1;
case 5: if ((insn & 0x0f000000u) == 0x0a000000u ||
        (insn & 0x0f000000u) == 0x0b000000u) return 1;
return -1;
case 6: if ((insn & 0x0f000e10u) == 0x0e000a00u ||     /* VFP CDP          */
               (insn & 0x0e000e00u) == 0x0c000a00u) return 21;
return -1;
case 7: if ((insn & 0x0f000010u) == 0x0e000010u) return 20;
else if ((insn & 0x0f000e10u) == 0x0e000a00u ||     /* VFP CDP          */
               (insn & 0x0e000e00u) == 0x0c000a00u) return 21;
else if ((insn & 0x0f000000u) == 0x0f000000u) return 22;
return -1;
}return -2;}
int main(void){for(unsigned cond=0;cond<2;cond++)for(uint32_t payload=0;payload<0x10000000u;payload++){uint32_t insn=payload|(cond?0xe0000000u:0);if(original(insn)!=candidate(insn)){printf("mismatch %08x\n",insn);return 1;}}puts("PASS 536870912 decoder routes");}
