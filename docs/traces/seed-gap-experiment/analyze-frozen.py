from pathlib import Path
import json,statistics,math,gzip,hashlib
p=Path('.context/seed-gap');controller=json.loads((p/'frozen-controller.json').read_text()); assert len(controller['runs'])==4
rows=[];frames=[];references={}
for run in controller['runs']:
 assert run['exit']==0
 d=Path(run['directory']);r=json.loads((d/'report.json').read_text());assert r['passed'];assert len(r['actions'])==22
 for a in r['actions']:
  rows.append({'arm':run['arm'],'session':d.name,**a})
  raw=gzip.decompress((d/f"{a['round']}-{a['kind']}.rgba.gz").read_bytes());assert len(raw)==320*480*4
  body=raw[30*320*4:];h=hashlib.sha256(body).hexdigest();references.setdefault(a['kind'],h)
  frames.append({'session':d.name,'round':a['round'],'kind':a['kind'],'belowStatusSha256':h,'matchesFirstControl':h==references[a['kind']]})
out={'scope':'Same production CPU and unchanged compiler, original versus256-gap seed; control/candidate/candidate/control;11pairs/session,20warm samples/action/version. Normal clock/autosave. Readiness50ms sample and500ms confirmation. Separate fixed replay validates state; timing is cloudChrome, notMac or all-app acceptance.','summary':{},'frames':frames,'allFramesMatch':all(x['matchesFirstControl']for x in frames)}
for kind in ['settings','home']:
 out['summary'][kind]={}
 for arm in ['control','candidate']:
  acts=[a for a in rows if a['arm']==arm and a['kind']==kind];v=sorted(a['releaseToReadyMs']for a in acts if a['round']>0);assert len(v)==20
  out['summary'][kind][arm]={'warmSamples':len(v),'medianMs':statistics.median(v),'p95Ms':v[math.ceil(len(v)*.95)-1],'firstMs':[a['releaseToReadyMs']for a in acts if a['round']==0],'warmMs':v}
 c=out['summary'][kind]['control'];b=out['summary'][kind]['candidate'];out['summary'][kind]['medianReductionPercent']=100*(1-b['medianMs']/c['medianMs']);out['summary'][kind]['p95DeltaMs']=b['p95Ms']-c['p95Ms']
(p/'browser-summary.json').write_text(json.dumps(out,indent=2));print(json.dumps({k:v for k,v in out.items() if k!='frames'},indent=2))
