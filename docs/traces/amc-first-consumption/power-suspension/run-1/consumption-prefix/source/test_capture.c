#include "capture.h"
#include <assert.h>
#include <stdio.h>
int main(void){uint8_t backing[32]={0};amc_physical_ram r={backing,0x1000,32};size_t o=99,n=99;unsigned tests=0;
#define CHECK(x) do{assert(x);tests++;}while(0)
CHECK(capture_span(&r,0x1000,8,&o,&n)&&o==0&&n==32);CHECK(capture_span(&r,0x101c,1,&o,&n)&&o==28&&n==4);
CHECK(!capture_span(&r,0x101c,2,&o,&n));CHECK(!capture_span(&r,0xffc,1,&o,&n));CHECK(!capture_span(&r,0x1001,1,&o,&n));CHECK(!capture_span(&r,0x1000,0,&o,&n));CHECK(!capture_span(&r,0x1000,0xffffffff,&o,&n));CHECK(o==28&&n==4);
r.base=0xfffffff0;CHECK(capture_span(&r,0xfffffff0,4,&o,&n));CHECK(!capture_span(&r,0xfffffff0,5,&o,&n));r.bytes=0;CHECK(!capture_span(&r,0xfffffff0,1,&o,&n));CHECK(!capture_span(0,0,1,&o,&n));printf("capture bounds: %u checks passed\n",tests);}
