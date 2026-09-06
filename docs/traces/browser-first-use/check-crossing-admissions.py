"""Read-only executable-entry check; census counts are not time attribution."""
import json,hashlib
from pathlib import Path
root=Path(__file__).resolve().parent
r=json.loads((root/'crossing-input.json').read_text())
defs={(d['pc'],d['thumb']):d for d in r['regions']}
counts={'recorded':0,'regionPresent':0,'entryAdmitted':0}
for e in r['pageEdges']:
 n=e['count'];counts['recorded']+=n;d=defs.get((int(e['destination'],16),e['thumb']))
 counts['regionPresent']+=n*bool(d)
 if d:
  bit=(int(e['pc'],16)&1023)>>(1 if e['thumb'] else 2)
  counts['entryAdmitted']+=n*bool(d['masks'][bit>>5] & (1<<(bit&31)))
assert counts['recorded']==r['originalPageExits']
(root/'crossing-admissions.json').write_text(json.dumps({'counts':counts,'entryPercent':100*counts['entryAdmitted']/counts['recorded'],'scope':'Historical original-seed100M MBX census; no new runtime or timing measurement','originalRuntime':r['runtimeSha256'],'originalSeed':r['seedSha256']},indent=2)+'\n')
print(counts)
