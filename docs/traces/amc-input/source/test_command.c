#define main previous_post_contract
#include "test_post.c"
#undef main
static void prepared(amc_idle_t*s){setup(s);CHECK(amc_idle_observe_upload(s));for(unsigned i=0;i<19;i++)CHECK(amc_idle_write(s,afterO[i],4,afterV[i])==AMC_ACCEPT);}
int main(void){
 previous_post_contract();
 amc_idle_t s,old;uint32_t v;
 amc_idle_init(&s);old=s;CHECK(!amc_idle_observe_command(&s));CHECK(!memcmp(&s,&old,sizeof s));
 prepared(&s);old=s;CHECK(amc_idle_observe_command(&s));old.armed=true;CHECK(!memcmp(&s,&old,sizeof s));
 CHECK(amc_idle_read(&s,0x10,4,&v)==AMC_ACCEPT&&v==0);CHECK(amc_idle_read(&s,0xb18,4,&v)==AMC_ACCEPT&&v==0x100);CHECK(amc_idle_read(&s,0x101c,4,&v)==AMC_ACCEPT&&v==0);
 old=s;CHECK(!amc_idle_observe_command(&s));CHECK(!memcmp(&s,&old,sizeof s));
 for(unsigned off=0;off<0x2000;off+=4)reject(&s,off,4,1);
 for(unsigned i=0;i<19;i++){prepared(&s);s.post_prefix=i;old=s;CHECK(!amc_idle_observe_command(&s));CHECK(!memcmp(&s,&old,sizeof s));}
 for(unsigned i=0;i<3;i++){prepared(&s);CHECK(amc_idle_observe_command(&s));amc_idle_dependencies(&s,i==0?8:i==1?16:0,i==2?16:0);CHECK(!s.armed);old=s;CHECK(!amc_idle_observe_command(&s));CHECK(!memcmp(&s,&old,sizeof s));}
 prepared(&s);s.input_pending=true;old=s;CHECK(!amc_idle_observe_command(&s));CHECK(!memcmp(&s,&old,sizeof s));
 printf("isolated armed observation: %u total checks passed\n",checks);
 return 0;
}
