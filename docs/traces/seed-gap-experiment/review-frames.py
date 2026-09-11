from pathlib import Path
import gzip,json,collections,hashlib
b=Path('.context/seed-gap');summary=json.loads((b/'browser-summary.json').read_text());ref={k:gzip.decompress((b/'browser-0-control'/f'0-{k}.rgba.gz').read_bytes())for k in ['settings','home']};rows=[]
for f in summary['frames']:
 p=b/f['session']/f"{f['round']}-{f['kind']}.rgba.gz";raw=gzip.decompress(p.read_bytes());expected=ref[f['kind']];assert len(raw)==len(expected)==614400
 changes=[(i,abs(x-y))for i,(x,y)in enumerate(zip(raw[38400:],expected[38400:]),38400)if x!=y]
 pixels={i//4 for i,d in changes};xs=[i%320 for i in pixels];ys=[i//320 for i in pixels]
 row={**f,'rawFile':str(p),'wholeRawSha256':hashlib.sha256(raw).hexdigest(),'changedChannels':len(changes),'changedPixels':len(pixels),'maxChannelDelta':max((d for i,d in changes),default=0),'alphaChanges':sum(i%4==3 for i,d in changes),'bboxInclusive':([min(xs),min(ys),max(xs),max(ys)]if pixels else None),'exactBelow30':not changes,'differencesOutsideRightmost10Columns':sum(i%320<310 for i in pixels)}
 assert row['exactBelow30']==f['matchesFirstControl'];rows.append(row)
failed=[r for r in rows if not r['exactBelow30']];out={'scope':'Artifact-only actual byte differences against frozen first control same-labelled raw frame below30 rows. No normalization, new crop, or widened tolerance. Exact gate failures remain failures.','frames':rows,'total':len(rows),'exact':len(rows)-len(failed),'mismatches':len(failed),'mismatchesBySession':dict(collections.Counter(r['session']for r in failed)),'mismatchesByKind':dict(collections.Counter(r['kind']for r in failed)),'worstMaxChannelDelta':max(r['maxChannelDelta']for r in rows),'unionBBox':([min(r['bboxInclusive'][0]for r in failed),min(r['bboxInclusive'][1]for r in failed),max(r['bboxInclusive'][2]for r in failed),max(r['bboxInclusive'][3]for r in failed)]if failed else None),'allAlphasExact':all(r['alphaChanges']==0 for r in rows)}
(b/'frame-review.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:v for k,v in out.items()if k!='frames'},indent=2));print('failures',[(r['session'],r['round'],r['changedPixels'],r['maxChannelDelta'],r['bboxInclusive'])for r in failed])
