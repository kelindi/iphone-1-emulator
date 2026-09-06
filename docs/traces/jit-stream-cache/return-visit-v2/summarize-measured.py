from pathlib import Path
import json,statistics,math
b=Path(__file__).resolve().parent;s=json.loads((b/'measured/series.json').read_text());rows=[];allstages=[]
def describe(values):
 return dict(n=len(values),valuesMs=values,medianMs=statistics.median(values),p95NearestRankMs=sorted(values)[math.ceil(.95*len(values))-1],meanMs=statistics.mean(values)) if values else None
for run in s['runs']:
 r=run.get('report');folder=b/'measured'/f"{run['index']}-{run['variant']}"
 if r is None and (folder/'report-partial.json').exists():r=json.loads((folder/'report-partial.json').read_text())
 if r is None:continue
 actions=r['actions'];allstages.extend(dict(index=run['index'],variant=run['variant'],**a) for a in actions)
 rows.append(dict(index=run['index'],variant=run['variant'],elapsedSeconds=run['elapsedSeconds'],exitCode=run['exitCode'],passed=r.get('passed'),strictStages=len(actions),failedStages=[dict(label=a['label'],kind=a['kind'],validation=a['validation']) for a in actions if not a['validation']['passed']],first={k:[a['releaseToReadyMs'] for a in actions if a['round']==1 and a['kind']==k] for k in ['settings','home']},warm={k:describe([a['releaseToReadyMs'] for a in actions if a['round']>1 and a['kind']==k]) for k in ['settings','home']},cacheAccepted=r.get('cacheReceipt',{}).get('accepted'),assets=r['assets'],scopedProcessesClosed=len(run.get('scopedProcessesClosed',[])),validationPhaseMs=describe([a['validation']['phaseMs']['totalSettled'] for a in actions])))
combined={v:{'first':{k:[a['releaseToReadyMs'] for a in allstages if a['variant']==v and a['round']==1 and a['kind']==k] for k in ['settings','home']},'warm':{k:describe([a['releaseToReadyMs'] for a in allstages if a['variant']==v and a['round']>1 and a['kind']==k]) for k in ['settings','home']}} for v in ['original','candidate']}
report=dict(scope='CONTAMINATED diagnostic: external browser/encoding work overlapped later arms. No quiet speed acceptance or production promotion. Frozen no-cache/ETag V2, all orders retained, first visits never called p95.',completedSeries=s['passed'],quietAcceptance=False,contamination=json.loads((b/'contamination.json').read_text()),failure=s.get('failure'),rows=rows,combined=combined)
(b/'measured-summary.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['completedSeries','failure','combined']},indent=2))
