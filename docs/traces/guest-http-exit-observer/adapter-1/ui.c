#include "ui.h"
#include <stddef.h>
#include "ui-data.h"
unsigned ui_error(const uint8_t*actual,const uint8_t*expected,const reference*r){if(!actual||!expected||!r||!r->w||!r->h||r->x>=320||r->y>=480||r->w>320-r->x||r->h>480-r->y)return UINT32_MAX;uint64_t total=0;for(unsigned y=r->y;y<r->y+r->h;y++)for(unsigned x=r->x*3;x<(r->x+r->w)*3;x++){unsigned i=y*960+x;int d=(int)actual[i]-expected[i];total+=(unsigned)(d<0?-d:d);}return(unsigned)((total*1000)/(r->w*r->h*3));}
void ui_observe(ui_state*u,bool match,uint64_t ticks){if(u->failed||u->done||u->phase!=0&&u->phase!=3||ticks==u->last_scan)return;u->last_scan=ticks;u->stable=match?u->stable+1:0;}
int ui_step(ui_state*u,uint64_t n,uint64_t t,bool owner_ready,tap*out){if(!u||!out||u->done||u->failed)return 0;if(n<u->stage_insn||t<u->stage_ticks){u->failed=true;u->reason="ui-clock-backward";return 0;}if(n-u->stage_insn>=1500000000ull||t-u->stage_ticks>=180000000ull){u->failed=true;u->reason="ui-stage-cap";return 0;}if(!owner_ready)return 0;
 if(u->stage==sizeof stages/sizeof*stages){if(u->phase==0){out->x=110;out->y=310;u->down=true;u->phase=1;u->phase_ticks=t;return 1;}if(u->phase==1&&t-u->phase_ticks>=1080000){u->down=false;u->phase=4;u->phase_ticks=t;return -1;}if(u->phase==4&&(n-u->stage_insn>=1000000000ull||t-u->phase_ticks>=180000000ull)){u->failed=true;u->reason="diagnostic-tail-cap-no-exit-witness";}return 0;}
 const stage*s=&stages[u->stage];
 if(u->phase==0&&u->stable>=2){u->stable=0;u->phase=1;u->phase_ticks=t;u->down=true;*out=s->taps[u->tap];return 1;}
 if(u->phase==1&&t-u->phase_ticks>=1080000){u->down=false;u->phase=2;u->phase_ticks=t;return -1;}
 if(u->phase==2&&t-u->phase_ticks>=3900000){u->tap++;if(u->tap<s->count){u->down=true;u->phase=1;u->phase_ticks=t;*out=s->taps[u->tap];return 1;}u->phase=3;u->stable=0;u->last_scan=0;}
 if(u->phase==3&&u->stable>=2){u->stage++;u->phase=0;u->tap=0;u->stable=0;u->last_scan=0;u->stage_insn=n;u->stage_ticks=t;}
 return 0;
}
