#include "ui.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){uint8_t*p=calloc(1,460800),*q=calloc(1,460800);C(p&&q);reference r={0,20,320,440,"x"};C(ui_error(p,q,&r)==0);memset(q,255,460800);C(ui_error(p,q,&r)==255000);r.w=321;C(ui_error(p,q,&r)==UINT32_MAX);tap point={0};ui_state u={0};ui_observe(&u,true,1);ui_observe(&u,true,1);C(u.stable==1);C(ui_step(&u,1,1,true,&point)==0);ui_observe(&u,true,2);C(ui_step(&u,2,2,false,&point)==0);C(ui_step(&u,2,2,true,&point)==1&&u.down&&point.x==160&&point.y==300);C(ui_step(&u,3,1080001,true,&point)==0);C(ui_step(&u,4,1080002,true,&point)==-1&&!u.down);C(ui_step(&u,5,4980001,true,&point)==0&&u.phase==2);C(ui_step(&u,6,4980002,true,&point)==0&&u.phase==3);ui_observe(&u,true,5000000);ui_observe(&u,false,5000001);C(u.stable==0);ui_observe(&u,true,5000002);ui_observe(&u,true,5000003);C(ui_step(&u,7,5000003,true,&point)==0&&u.stage==1);
 u=(ui_state){0};C(ui_step(&u,1500000000,1,true,&point)==0&&u.failed);u=(ui_state){0};C(ui_step(&u,1,180000000,true,&point)==0&&u.failed);u=(ui_state){.stage_ticks=10};C(ui_step(&u,1,9,true,&point)==0&&u.failed);
 u=(ui_state){.stage=8};C(ui_step(&u,1,1,true,&point)==1&&point.x==110&&point.y==310);C(ui_step(&u,2,1080001,true,&point)==-1);C(ui_step(&u,1000000000,1080002,true,&point)==0&&u.failed);free(p);free(q);printf("PASS %u UI predicate/hold/gap/stability/stage/tail bound checks; synthetic only\n",checks);return 0;}
