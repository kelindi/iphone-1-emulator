from pathlib import Path
import json,subprocess,hashlib
p=Path('.context/jit-fallback-census');actual=Path('.context/jit-mbx-profile/unsupported-mnemonics.json');d=json.loads(actual.read_text());keys=[]
for family in d['families']:
 for e in family['examples']:keys.append((int(e['pc'],16),int(e['raw'],16),int(e['thumb'])))
keys=sorted(set(keys));source='#include "census.h"\n#include <stdio.h>\nint main(void){fc_reset();fc_start();\n'
for pc,op,t in keys:source+=f'fc_begin({pc}u,{t});fc_fetch({op}u);fc_end(0,1);\n'
source+='printf("actual %llu %llu\\n",(unsigned long long)fc_summary()->calls,(unsigned long long)fc_summary()->overflow);fc_reset();fc_start();\n'
source+='for(unsigned i=0;i<2048;i++){fc_begin(i*262144u,0);fc_fetch(0xe1a00000);fc_end(0,1);}printf("synthetic %llu %llu\\n",(unsigned long long)fc_summary()->calls,(unsigned long long)fc_summary()->overflow);}\n'
(p/'test-distribution.c').write_text(source);rows=[]
for version,src in [('old',p/'v1-overflow/census.c'),('new',p/'census.c')]:
 exe=p/('distribution-'+version);subprocess.run(['cc','-std=c11','-O1','-I'+str(p),str(src),str(p/'test-distribution.c'),'-o',str(exe)],check=True)
 result=subprocess.check_output([str(exe)],text=True);row={'version':version,'counts':{line.split()[0]:{'calls':int(line.split()[1]),'overflow':int(line.split()[2])} for line in result.splitlines()}};rows.append(row)
assert rows[0]['counts']['synthetic']['overflow']>0
assert rows[1]['counts']['synthetic']['overflow']==0 and rows[1]['counts']['actual']['overflow']==0
report={'passed':True,'actualSource':str(actual),'actualSourceSha256':hashlib.sha256(actual.read_bytes()).hexdigest(),'actualKeys':len(keys),'scope':'Retained actual unsupported-exit PC/opcode/mode examples, not a recovered failed-census table. Admission/provenance are intentionally NONE/outside here. First overflow process did not persist rows; exact triggering keys and stop instruction count are unavailable. Synthetic high-PC-bit sequence proves old clustering without a guest rerun. Current repair uses explicitly authorized524288rows; probe/capture caps unchanged.','results':rows}
(p/'distribution-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
