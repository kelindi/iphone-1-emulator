"""Prepare observation-only CLI copy. Does not run guest or alter core."""
from pathlib import Path
B=Path(__file__).resolve().parent
s=Path('tools/iphone1.c').read_text();(B/'iphone1-original.c').write_text(s)
text=(B/'iphone1-own-program').read_bytes()[0xf04:0xfd8]
state='''
/* Qualification state belongs to this host probe, never to guest snapshots. */
static bool hello_seen, hello_serial, hello_write_done, hello_exit_seen;
static bool hello_failed;
static uint32_t hello_ttbr, hello_pid;
static char hello_line[256]; static unsigned hello_line_length;
static unsigned hello_entries;
static const unsigned char hello_text[] = {'''+','.join(str(x) for x in text)+'''};
static void hello_serial_byte(unsigned char ch) {
    if(ch=='\\r') return;
    if(ch=='\\n') {
        hello_line[hello_line_length]=0;
        char expected[80];
        snprintf(expected,sizeof expected,"IPHONE1_OWN_PROGRAM_V1 pid=%010u",hello_pid);
        if(hello_seen && hello_pid>1 && !strcmp(hello_line,expected)) hello_serial=true;
        hello_line_length=0;
    } else if(hello_line_length+1<sizeof hello_line) hello_line[hello_line_length++]=ch;
    else hello_line_length=0;
}
'''
s=s.replace('typedef struct { FILE *serial;',state+'\ntypedef struct { FILE *serial;',1)
s=s.replace('tap_t *t=ctx; putchar((int)c); fflush(stdout);','tap_t *t=ctx; hello_serial_byte(c); putchar((int)c); fflush(stdout);',1)
a=s.index('static bool watch_hook(void *ctx)');z=s.index('\n#define TRACE_RING',a)
s=s[:a]+'''static bool watch_hook(void *ctx) {
    (void)ctx; s5l8900_t *m=(s5l8900_t*)g_watch_m;
    uint32_t pc=m->cpu.r[15];
    if((m->cpu.cpsr & 0x3fu)!=0x10u) return false;
    if(pc==0x1f04u) {
        for(unsigned i=0;i<sizeof hello_text;i+=4) {
            uint32_t w;
            if(!read_va32(m,pc+i,&w))return false;
            for(unsigned j=0;j<4 && i+j<sizeof hello_text;j++)
                if((unsigned char)(w>>(8*j))!=hello_text[i+j])return false;
        }
        hello_seen=true; hello_ttbr=m->cpu.cp15.ttbr0; hello_entries++;
        printf("HELLO entry cycle=%llu ttbr=%08x pc=%08x cpsr=%08x exact_text_bytes=%zu\\n",(unsigned long long)m->cpu.cycles,hello_ttbr,pc,m->cpu.cpsr,sizeof hello_text);
    }
    if(!hello_seen || m->cpu.cp15.ttbr0!=hello_ttbr) return false;
    if(pc==0x1f4cu) {
        hello_pid=m->cpu.r[0]; if(hello_pid<=1 || hello_pid>0x7fffffffu)hello_failed=true;
        printf("HELLO getpid_return cycle=%llu pid=%u\\n",(unsigned long long)m->cpu.cycles,hello_pid);
    }
    if(pc==0x30009860u) {
        char expected[80];int len=snprintf(expected,sizeof expected,"IPHONE1_OWN_PROGRAM_V1 pid=%010u\\n",hello_pid);
        if(m->cpu.r[0]!=1 || m->cpu.r[2]!=(unsigned)len)hello_failed=true;
        for(unsigned i=0;i<(unsigned)len;i++) {
            uint32_t a=m->cpu.r[1]+i,w;
            if(!read_va32(m,a&~3u,&w) || (unsigned char)(w>>(8*(a&3u)))!=(unsigned char)expected[i]) hello_failed=true;
        }
        printf("HELLO original_write cycle=%llu fd=%u length=%u exact=%d\\n",(unsigned long long)m->cpu.cycles,m->cpu.r[0],m->cpu.r[2],!hello_failed);
    }
    if(pc==0x1fa0u) {
        hello_write_done=m->cpu.r[0]==38u;if(!hello_write_done)hello_failed=true;
        printf("HELLO write_return cycle=%llu bytes=%u\\n",(unsigned long long)m->cpu.cycles,m->cpu.r[0]);
    }
    if(pc==0x300099f0u) {
        hello_exit_seen=m->cpu.r[0]==0u;if(!hello_exit_seen)hello_failed=true;
        printf("HELLO original_exit_entry cycle=%llu status=%u\\n",(unsigned long long)m->cpu.cycles,m->cpu.r[0]);
    }
    return false; /* Always let the actual next guest instruction retire. */
}
''' + s[z:]
s=s.replace('while (total<steps) {','''while (total<steps) {
        if(hello_failed) { reason="hello-observation-failed";break; }
        if(hello_serial && hello_write_done && hello_exit_seen) { reason="hello-qualified";break; }
''',1)
s=s.replace('puts("");\n    if (g_trace_n)', '''puts("");
    printf("HELLO final seen=%d serial=%d write=%d exit_entry=%d failed=%d entries=%u ttbr=%08x pid=%u\\n",hello_seen,hello_serial,hello_write_done,hello_exit_seen,hello_failed,hello_entries,hello_ttbr,hello_pid);
    if (g_trace_n)''',1)
(B/'probe.c').write_text(s)
