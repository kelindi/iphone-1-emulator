#define main previous_post_contract
#include "test_post.c"
#undef main
static void armed(amc_idle_t*s){
 setup(s);CHECK(amc_idle_observe_upload(s));for(unsigned i=0;i<19;i++)CHECK(amc_idle_write(s,afterO[i],4,afterV[i])==AMC_ACCEPT);CHECK(amc_idle_observe_command(s));
 const uint32_t o[]={0x204,0x184,0xb0c,0x1018},v[]={1,7,0x2000,1};for(unsigned i=0;i<4;i++)CHECK(amc_idle_write(s,o[i],4,v[i])==AMC_ACCEPT);
}
static void deny_input(amc_idle_t*s,uint32_t a,uint32_t w,uint32_t n,uint32_t base,uint32_t size){amc_idle_t old=*s;CHECK(!amc_idle_observe_input(s,a,w,n,base,size));CHECK(!memcmp(s,&old,sizeof old));}
int main(void){
 amc_idle_t s,old;uint32_t v;const uint32_t a=0x14a8b000,w=0x1000,n=0x4000,base=0x08000000,size=0x10000000;
 amc_idle_init(&s);deny_input(&s,a,w,n,base,size);armed(&s);
 deny_input(&s,a,0,0,base,size);deny_input(&s,a,w,n+1,base,size);deny_input(&s,a,0x4001,0x10001,base,size);deny_input(&s,a+1,w,n,base,size);deny_input(&s,base-4,w,n,base,size);deny_input(&s,base+size-4,w,n,base,size);deny_input(&s,0xfffffffcu,2,8,0,UINT32_MAX);deny_input(&s,a,UINT32_MAX,n,base,size);
 old=s;s.input_pending=true;deny_input(&s,a,w,n,base,size);s=old;
 CHECK(amc_idle_observe_input(&s,a,w,n,base,size));old.input_observed=true;old.input_address=a;old.input_words=w;old.input_bytes=n;CHECK(!memcmp(&old,&s,sizeof s));deny_input(&s,a,w,n,base,size);
 const uint32_t off[]={0x101c,0x100c,0x1000,0x1004,0x1008,0xa00},val[]={1,0,1,a,w,1};
 for(unsigned i=0;i<6;i++){
  for(unsigned o=0;o<0x2000;o+=4)if(o!=off[i])reject(&s,o,4,val[i]);
  reject(&s,off[i],2,val[i]);reject(&s,off[i],4,val[i]^2);reject(&s,0x100c,4,1);reject(&s,0x2000,4,3);
  old=s;CHECK(amc_idle_write(&s,off[i],4,val[i])==AMC_ACCEPT);old.input_config[i]=val[i];old.input_prefix++;CHECK(!memcmp(&s,&old,sizeof s));
  CHECK(amc_idle_read(&s,0xb18,4,&v)==AMC_ACCEPT&&v==0x100);CHECK(amc_idle_read(&s,0x10,4,&v)==AMC_ACCEPT&&v==0);CHECK(amc_idle_read(&s,0x101c,4,&v)==AMC_ACCEPT&&v==0);
 }
 old=s;CHECK(amc_idle_write(&s,0x100c,4,1)==AMC_INPUT_REQUEST);CHECK(!memcmp(&s,&old,sizeof s));
 for(unsigned o=0;o<0x2000;o+=4)if(o!=0x100c)reject(&s,o,4,1);
 for(unsigned nbytes=1;nbytes<=9;nbytes++){armed(&s);CHECK(amc_idle_observe_input(&s,a,(nbytes+3)/4,nbytes,base,size));CHECK(s.input_bytes==nbytes&&s.input_words==(nbytes+3)/4);}
 for(unsigned i=0;i<3;i++){armed(&s);CHECK(amc_idle_observe_input(&s,a,w,n,base,size));amc_idle_dependencies(&s,i==0?8:i==1?16:0,i==2?16:0);CHECK(!s.armed&&!s.input_observed&&!s.input_prefix&&!s.input_address&&!s.input_words&&!s.input_bytes);deny_input(&s,a,w,n,base,size);}
 printf("isolated unaccepted input: %u checks passed\n",checks);return 0;
}
