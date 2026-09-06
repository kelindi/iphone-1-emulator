from pathlib import Path
import json,subprocess,collections
p=Path('.context/jit-loop-evidence');r=json.loads((p/'existing.json').read_text());counts=collections.Counter()
selected=subprocess.check_output(['rg','^(300489|30096[b-c]|3009a[1-4]|c005a[b-c]|c0060[6-9a-b])','.context/speed-next/settings-profile.tsv'],text=True)
for line in selected.splitlines():
 pc,thumb,raw,count=line.split();pc=int(pc,16);count=int(count)
 for i,interval in enumerate(r['symbolRanges']):
  if interval['start']<=pc<interval['end']:counts[i]+=count
rows=[dict(r['symbolRanges'][i],recordedInstructions=n) for i,n in counts.items()]
result={'scope':'Historical software-renderer Settings PC census,557414127 recorded instructions. Existing nearest-symbol intervals only; not current MBX browser and not a timing attribution. No new guest execution.','totalRecordedInstructions':557414127,'rows':rows,'sumRecordedInstructions':sum(counts.values()),'percentRecordedInstructions':100*sum(counts.values())/557414127}
(p/'old-pc-counts.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
