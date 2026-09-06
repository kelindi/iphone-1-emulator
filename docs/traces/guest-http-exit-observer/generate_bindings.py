from pathlib import Path
import sys,json
sys.path.insert(0,'tools/legacy_services/guest_http')
from macho_audit import parse
B=Path(__file__).resolve().parent
r=json.loads((B/'addresses.json').read_text());lines=['/* Generated from pinned original file bytes, not an executed guest trace. */']
for kind,name in [('kernel','_exit1'),('kernel','_psignal'),('kernel','_sigexit_locked'),('own','_number')]:
 x=next(x for x in r['images'][kind]['hooks'] if x['name']==name);mode='ARM_MODE_SVC|ARM_CPSR_T' if kind=='kernel' else 'ARM_MODE_USR';data=','.join('0x'+x['entry32Hex'][i:i+2] for i in range(0,64,2));lines.append('static const qualified_site site_'+name[1:]+'={'+x['va']+','+mode+',{'+data+'}};')
a=parse(Path(r['images']['own']['path']));calls={'own_exit':0x1a84,'listener_poll_return':0x1c1c,'accept_return':0x1c74,'client_close_return':0x1cb4,'setsockopt_return':0x2434,'accept_errno':0x1d04,'poll_errno':0x1ce8}
for name,va in calls.items():
 seg=next(s for s in a[5] if s[1]<=va and va+32<=s[1]+s[4]);off=seg[3]+va-seg[1];data=','.join(hex(v) for v in a[0][off:off+32]);lines.append('static const qualified_site site_'+name+'={'+hex(va)+',ARM_MODE_USR,{'+data+'}};')
(B/'bindings.h').write_text('\n'.join(lines)+'\n')
