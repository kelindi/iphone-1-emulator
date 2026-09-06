#define main disposable_probe_main
#include "probe.c"
#undef main
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static void put(uint32_t va,uint32_t v){memcpy(machine.ram+va-machine.ram_base,&v,4);}
static void prepare(void){
 memset(&machine,0,sizeof machine);machine.ram_base=0x10000000;machine.ram_size=0x20000;machine.ram=calloc(1,machine.ram_size);
 CHECK(machine.ram);amc_idle_init(&idle);idle.enabled=idle.de_complete=idle.armed=true;idle.armed_prefix=4;
 input_seen=false;result=0;verified_driver=0x10001000;
 machine.cpu.r[0]=0x10010000;machine.cpu.r[1]=0x1000;machine.cpu.r[4]=0x10002000;machine.cpu.r[5]=verified_driver;machine.cpu.r[8]=0x4000;machine.cpu.r[14]=0xc0275f58;
 put(verified_driver+0xc0,2);put(verified_driver+0x110,0x10002000);put(verified_driver+0x128,0x10003000);
 put(0x10002000,0x10004000);put(0x10004100,0xc0147bff);put(0x10003000,0x10004000);
}
int main(void){
 for(unsigned scenario=0;scenario<11;scenario++){
  prepare();
  switch(scenario){
   case 0:break; /* Unknown vtable, despite valid method/data pointers. */
   case 1:machine.cpu.r[5]+=4;break;
   case 2:put(verified_driver+0xc0,3);break;
   case 3:put(verified_driver+0xd8,0x100);break;
   case 4:machine.cpu.r[0]++;break;
   case 5:machine.cpu.r[1]--;break;
   case 6:machine.cpu.r[8]=0x10001;break;
   case 7:put(verified_driver+0x114,0x10002000);break;
   case 8:put(verified_driver+0x128,0);break;
   case 9:put(0x10002000,0xfffffffcu);break;
   case 10:machine.cpu.r[0]=0x1001f000;break;
  }
  amc_idle_t old=idle;
  if(!setjmp(finish)){input_capture();CHECK(false);}
  CHECK(result==1);CHECK(!memcmp(&old,&idle,sizeof idle));CHECK(!input_seen);free(machine.ram);machine.ram=NULL;
 }
 printf("isolated input capture guards: %u checks passed\n",checks);return 0;
}
