#define main previous_post_contract
#include "test_post.c"
#undef main
static void prepared(amc_idle_t*s){setup(s);CHECK(amc_idle_observe_upload(s));for(unsigned i=0;i<19;i++)CHECK(amc_idle_write(s,afterO[i],4,afterV[i])==AMC_ACCEPT);CHECK(amc_idle_observe_command(s));}
int main(void){previous_post_contract();amc_idle_t s,old;uint32_t v;
 static const uint32_t off[]={0x204,0x184,0xb0c,0x1018}, val[]={1,7,0x2000,1};
 prepared(&s);
 for(unsigned i=0;i<4;i++){
  for(unsigned o=0;o<0x2000;o+=4)if(o!=off[i])reject(&s,o,4,val[i]);
  reject(&s,off[i],2,val[i]);reject(&s,off[i],4,val[i]^2);
  old=s;CHECK(amc_idle_write(&s,off[i],4,val[i])==AMC_ACCEPT);old.armed_config[i]=val[i];old.armed_prefix++;CHECK(!memcmp(&s,&old,sizeof s));
  CHECK(amc_idle_read(&s,0xb18,4,&v)==AMC_ACCEPT&&v==0x100);CHECK(amc_idle_read(&s,0x10,4,&v)==AMC_ACCEPT&&v==0);CHECK(amc_idle_read(&s,0x101c,4,&v)==AMC_ACCEPT&&v==0);
  old=s;CHECK(!amc_idle_observe_command(&s));CHECK(!memcmp(&s,&old,sizeof s));
 }
 for(unsigned o=0;o<0x2000;o+=4)reject(&s,o,4,1);
 reject(&s,0x2000,4,3);old=s;v=0x12345678;CHECK(amc_idle_read(&s,0x184,4,&v)==AMC_REJECT);CHECK(v==0x12345678);CHECK(!memcmp(&s,&old,sizeof s));
 for(unsigned i=0;i<3;i++){prepared(&s);amc_idle_dependencies(&s,i==0?8:i==1?16:0,i==2?16:0);CHECK(!s.armed&&!s.armed_prefix);reject(&s,0x204,4,1);}
 printf("isolated armed configuration: %u checks passed\n",checks);return 0;}
