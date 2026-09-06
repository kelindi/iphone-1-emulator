#ifndef AMC_CAPTURE_H
#define AMC_CAPTURE_H
#include "amc_engine.h"
/* Read-only physical bounds; outputs are assigned only for a valid span. */
static bool capture_span(const amc_physical_ram *ram,uint32_t address,uint32_t words,size_t *offset,size_t *bytes){
 uint64_t size=(uint64_t)words*4,end=(uint64_t)address+size;
 if(!ram||!ram->bytes||!words||(address&3)||address<ram->base||end>0x100000000ull||end>(uint64_t)ram->base+ram->size)return false;
 *offset=(size_t)(address-ram->base);*bytes=(size_t)size;return true;
}
#endif
