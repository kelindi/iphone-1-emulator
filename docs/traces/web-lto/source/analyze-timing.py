from pathlib import Path
import json,statistics,hashlib,subprocess,time
p=Path('.context/web-lto');r=json.loads((p/'timing/report.json').read_text());c=json.loads((p/'timing/controller.json').read_text());assert r['passed']and c['passed']and c['processClosed']and c['exitCode']==0 and not Path('/proc',str(c['pid'])).exists()
assert [x['label']for x in r['warmup']]==['control','candidate']and[x['label']for x in r['runs']]==['control','candidate','candidate','control'];ref=r['warmup'][0]
for row in r['warmup']+r['runs']:
 assert row['checkpoints']==ref['checkpoints']and row['frame']==ref['frame'];assert len(row['checkpoints'])==6 and row['checkpoints'][-1]['done']==600000000
assert ref['checkpoints'][0]['hash']==json.loads((p/'retained-control-100m.json').read_text())['checkpoints'][-1]['rawSha256']
a1,b1,b2,a2=r['runs'];stats={}
for field in ['processCpuMs','runMs']:
 av=[a1[field],a2[field]];bv=[b1[field],b2[field]];stats[field]={'original':av,'candidate':bv,'forwardReductionPercent':100*(a1[field]-b1[field])/a1[field],'reverseReductionPercent':100*(a2[field]-b2[field])/a2[field],'originalMean':statistics.mean(av),'candidateMean':statistics.mean(bv),'combinedReductionPercent':100*(sum(av)-sum(bv))/sum(av)}
out={'passed':True,'scope':'One coordinated fixed-work Node/WASM ABBA; both600M warmups and measured state/counter/frame exact, original seed and guards. No normalization. No first-use/browser navigation improvement claim. Baseline VFP conformance remains595pass/12fail.','metrics':stats,'warmupCheckpointReceipts':12,'measuredCheckpointReceipts':24,'allCheckpointsEqualReleasedControlWarmup':True,'known100MReferenceExact':True,'finalFrame':ref['frame'],'finalCounters':ref['checkpoints'][-1],'reportSha256':hashlib.sha256((p/'timing/report.json').read_bytes()).hexdigest(),'processClosed':True};(p/'timing/analysis.json').write_text(json.dumps(out,indent=2))
(p/'timing-process-after.json').write_text(json.dumps({'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'scope':'Redacted observation only: PID PPID COMM CPUTIME, no command arguments','processes':subprocess.check_output(['ps','-eo','pid,ppid,comm,cputime'],text=True)},indent=2));print(json.dumps(out,indent=2))
