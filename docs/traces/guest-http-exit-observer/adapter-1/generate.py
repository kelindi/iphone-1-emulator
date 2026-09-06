from pathlib import Path
import sys,json,struct,hashlib
sys.path.insert(0,'tools/legacy_services/guest_http');from macho_audit import parse
B=Path(__file__).resolve().parent
paths={'own':Path('.context/guest-http-default/build-quiet-2/iphone1-local-http'),'kernel':Path('web/firmware/kernel_1A543a.macho'),'lib':Path('.context/guest-hello/original-readonly/usr__lib__libSystem.B.dylib')}
images={k:parse(p) for k,p in paths.items()}
def data(k,va,n):
 a=images[k];s=next(s for s in a[5] if s[1]<=va and va+n<=s[1]+s[4]);o=s[3]+va-s[1];return a[0][o:o+n]
lines=['/* Original-file code witnesses; generated, never patched into guest. */'];receipts=[]
def arr(name,k,va,n=32):
 d=data(k,va,n);lines.append('static const unsigned char '+name+'[]={'+','.join(hex(x) for x in d)+'};');receipts.append({'name':name,'image':k,'va':hex(va),'bytes':n,'sha256':hashlib.sha256(d).hexdigest()})
calls=[('exit',0x1a84,5,0x4000,0x300099f0,4,0,0,0),('poll',0x1c18,3,0x4018,0x30067ae0,4,0x1ce4,0x1ce8,0),('accept',0x1c70,3,0x401c,0x30030520,4,0x1d00,0x1d04,2),('close',0x1cb0,1,0x4020,0x30009880,4,0,0,0),('setsockopt',0x2430,5,0x4038,0x3000e8b0,16,0x2448,0x244c,2),('console',0x219c,3,0x4024,0x30009860,4,0x21c8,0x21cc,0)]
for name,pc,reg,slot,va,svc,ec,er,dst in calls:
 arr('own_'+name,'own',pc);arr('lib_'+name,'lib',va,32);arr('return_'+name,'own',pc+4)
 if ec:arr('errcall_'+name,'own',ec);arr('errread_'+name,'own',er);arr('errafter_'+name,'own',er+4)
arr('lib_error','lib',0x30001bb0,32)
for name,va,n in [('exit1',0xc00f7094,32),('psignal',0xc00fd790,10),('sigexit',0xc00fdf6c,32),('statuspre',0xc00f710e,2),('statuspost',0xc00f7110,4),('setthread',0xc0060028,12),('currenttask',0xc0026040,10),('bsdtask',0xc002b5ac,8),('bsdthread',0xc002b5bc,8)]:arr('kernel_'+name,'kernel',va,n)
lines.append('static const callsite calls[]={')
for name,pc,reg,slot,va,svc,ec,er,dst in calls:
 lines.append('{"'+name+'",'+','.join(hex(x) for x in [pc,reg,slot,va,svc,ec,er,dst])+',own_'+name+',lib_'+name+',return_'+name+','+('errcall_'+name+',errread_'+name+',errafter_'+name if ec else 'NULL,NULL,NULL')+'},')
lines.append('};')
(B/'code.h').write_text('\n'.join(lines)+'\n');(B/'code-pins.json').write_text(json.dumps({'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths.values()},'witnesses':receipts},indent=2)+'\n')
