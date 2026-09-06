"""Generate observation-only CLI for independently authored program. No guest run."""
from pathlib import Path
import json
B=Path(__file__).resolve().parent
info=json.loads((B/'binary-audit.json').read_text());b=(B/'iphone1-own-loopback').read_bytes();text=info['text'];code=b[text['fileOffset']:text['fileOffset']+text['size']]
s=Path('tools/iphone1.c').read_text();(B/'iphone1-original.c').write_text(s)
state='''
static bool own_seen, own_pass_written, own_serial_pass, own_exit_seen, own_failed;
static uint32_t own_ttbr, own_exit_status; static unsigned own_entries;
static char own_line[256]; static unsigned own_line_n;
static const unsigned char own_text[]={'''+','.join(map(str,code))+'''};
static void own_serial_byte(unsigned char c){if(c=='\\r')return;if(c=='\\n'){own_line[own_line_n]=0;if(own_seen&&!strcmp(own_line,"LOOPBACK_PASS_V1"))own_serial_pass=true;own_line_n=0;}else if(own_line_n+1<sizeof own_line)own_line[own_line_n++]=c;else own_line_n=0;}
'''
s=s.replace('typedef struct { FILE *serial;',state+'\ntypedef struct { FILE *serial;',1).replace('tap_t *t=ctx; putchar((int)c); fflush(stdout);','tap_t *t=ctx; own_serial_byte(c); putchar((int)c); fflush(stdout);',1)
a=s.index('static bool watch_hook(void *ctx)');z=s.index('\n#define TRACE_RING',a)
hook='''static bool own_read(s5l8900_t*m,uint32_t address,void*dst,unsigned n){unsigned char*p=dst;if(n&&address>UINT32_MAX-(n-1))return false;for(unsigned i=0;i<n;i++){uint32_t a=address+i,w;if(!read_va32(m,a&~3u,&w))return false;p[i]=(unsigned char)(w>>(8*(a&3u)));}return true;}
static bool own_string(s5l8900_t*m,uint32_t address,char*s,unsigned n){for(unsigned i=0;i<n;i++){if(!own_read(m,address+i,s+i,1))return false;if(!s[i])return true;}return false;}
static bool watch_hook(void *ctx){(void)ctx;s5l8900_t*m=(s5l8900_t*)g_watch_m;uint32_t pc=m->cpu.r[15];if((m->cpu.cpsr&0x3fu)!=0x10u)return false;
 if(pc==ENTRY){unsigned char bytes[sizeof own_text];if(!own_read(m,TEXT,bytes,sizeof bytes)||memcmp(bytes,own_text,sizeof bytes))return false;own_seen=true;own_ttbr=m->cpu.cp15.ttbr0;own_entries++;if(own_entries!=1)own_failed=true;printf("OWN entry cycle=%llu pc=%08x cpsr=%08x ttbr=%08x exact_text=%zu\\n",(unsigned long long)m->cpu.cycles,pc,m->cpu.cpsr,own_ttbr,sizeof bytes);}
 if(!own_seen||m->cpu.cp15.ttbr0!=own_ttbr)return false;
 printf("OWN witness cycle=%llu pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\\n",(unsigned long long)m->cpu.cycles,pc,m->cpu.r[0],m->cpu.r[1],m->cpu.r[2],m->cpu.r[3],m->cpu.r[14]);
 if(pc==NUMBER){char label[80];if(!own_string(m,m->cpu.r[0],label,sizeof label)){own_failed=true;}else printf("OWN result label=%s value=%08x error_or_detail=%08x\\n",label,m->cpu.r[1],m->cpu.r[2]);}
 if(pc==BYTES){char label[80];unsigned char bytes[4096];unsigned n=m->cpu.r[2];if(n>sizeof bytes||!own_string(m,m->cpu.r[0],label,sizeof label)||!own_read(m,m->cpu.r[1],bytes,n)){own_failed=true;}else{printf("OWN bytes label=%s length=%u hex=",label,n);for(unsigned i=0;i<n;i++)printf("%02x",bytes[i]);puts("");}}
 if(pc==0x30009860u&&m->cpu.r[0]==1){static const char terminal[]="LOOPBACK_PASS_V1\\n";char data[sizeof terminal];if(m->cpu.r[2]==sizeof terminal-1&&own_read(m,m->cpu.r[1],data,sizeof terminal-1)&&!memcmp(data,terminal,sizeof terminal-1))own_pass_written=true;}
 if(pc==0x300099f0u){own_exit_seen=true;own_exit_status=m->cpu.r[0];if(own_exit_status)own_failed=true;printf("OWN exit_entry status=%u\\n",own_exit_status);}
 return false;
}
'''.replace('ENTRY',info['entry']+'u').replace('TEXT',hex(text['address'])+'u').replace('NUMBER',info['observations']['_number']+'u').replace('BYTES',info['observations']['_bytes']+'u')
s=s[:a]+hook+s[z:]
s=s.replace('while (total<steps) {','''while (total<steps) {
 if(own_failed){reason="own-loopback-observer-or-guest-failure";break;}
 if(own_exit_seen){reason=(own_pass_written&&own_serial_pass)?"own-loopback-qualified":"own-loopback-missing-proof";break;}
''',1)
s=s.replace('puts("");\n    if (g_trace_n)','''puts("");
 printf("OWN final seen=%d entries=%u pass_written=%d serial_pass=%d exit_seen=%d exit_status=%u failed=%d ttbr=%08x\\n",own_seen,own_entries,own_pass_written,own_serial_pass,own_exit_seen,own_exit_status,own_failed,own_ttbr);
 if (g_trace_n)''',1)
(B/'probe.c').write_text(s)
exports={r['symbol']:r['originalExportVA'] for r in info['imports']}
watch=[info['entry'],info['observations']['_number'],info['observations']['_bytes']]+[exports[n] for n in ['__exit','_socket','_bind','_listen','_connect','_accept','_read','_write','_ioctl','_sysctl','_getsockname','_getsockopt']]
assert len(watch)==15 and len(set(watch))==15
(B/'watch-pcs.json').write_text(json.dumps(watch,indent=2)+'\n')
