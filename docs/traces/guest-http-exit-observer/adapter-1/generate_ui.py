from pathlib import Path
import json
B=Path(__file__).resolve().parent;j=json.loads((B/'stages.json').read_text());r=json.loads((B/'predicate-checks.json').read_text())['rows'];idx={x['reference']:i for i,x in enumerate(r)}
s=['static const reference references[]={']
for q in r:s.append('{'+','.join(str(x)for x in q['region'])+',"'+q['reference']+'"},')
s+=['};','static const stage stages[]={']
for q in j['stages']:
 s.append('{"'+q['name']+'",'+','.join(str(x)for x in [idx[q['before']],idx[q['after']],len(q['taps'])])+',{'+','.join('{'+str(x)+','+str(y)+'}'for x,y in q['taps'])+'}},')
s.append('};');(B/'ui-data.h').write_text('\n'.join(s)+'\n')
