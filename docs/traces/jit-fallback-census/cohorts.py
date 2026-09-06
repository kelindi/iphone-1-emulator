from pathlib import Path
import json,gzip,hashlib
from collections import defaultdict
p=Path('.context/jit-fallback-census')
positions={(r['pc'],r['thumb']):r for r in json.loads(gzip.decompress((p/'emitter-positions.json.gz').read_bytes()))}
def masks(path):
 return {(r['pc'],r['thumb']):r['masks'] for r in json.loads(Path(path).read_text())}
oldpath='.context/jit-mbx-coverage/candidate-jit/regions.json';old=masks(oldpath);original=masks('web/jit/regions.json')
def admitted(table,pc,thumb):
 mask=table.get((pc&~1023,thumb));bit=(pc&1023)>>(1 if thumb else 2);return bool(mask and (mask[bit>>5]>>(bit&31))&1)
added=sum(bin((n&0xffffffff)&~(o&0xffffffff)).count("1") for key,ms in old.items() for n,o in zip(ms,original[key]));assert added==959
reports={}
for action in ['warm-settings','warm-home']:
 rows=json.loads(gzip.decompress((p/'browser-1'/f'{action}-rows.json.gz').read_bytes()));totals=defaultdict(int);pages=defaultdict(lambda:defaultdict(int));missing=defaultdict(lambda:defaultdict(int))
 for r in rows:
  if not(r['flags']&1) or r['flags']&16:continue
  thumb=bool(r['flags']&256);pos=positions[(r['pc'],thumb)]
  if not pos['emittable'] or len(pos['variants'])!=1:continue
  category='outside' if r['admission']==0 else 'missing' if r['admission']==2 else None
  if not category:continue
  reason=r['provenance'];cohort='directRejectedAttempt' if reason==(2 if category=='outside' else 3) else 'afterPositivePrefix' if reason==6 else 'noAttempt' if reason==0 else 'other'
  totals[f'{category}:{cohort}']+=r['count']
  if category=='outside':
   key=(r['pc']&~1023,thumb);pages[key][cohort]+=r['count'];pages[key]['pageChanged']=pos['pageChanged']
  else:
   covered=admitted(old,r['pc'],thumb);label='alreadyInParked959' if covered else 'newToParked959';totals[f'missing:{label}:{cohort}']+=r['count'];missing[(r['pc'],thumb)][label+':'+cohort]+=r['count']
 ranked=sorted((dict(pc=pc,hex=hex(pc),thumb=thumb,**v)for(pc,thumb),v in pages.items()),key=lambda x:x.get('directRejectedAttempt',0),reverse=True)
 budgets=[]
 for n in [8,16,32,64,128,256]:
  selected=ranked[:n];budgets.append(dict(pages=n,directRejectedAttempt=sum(x.get('directRejectedAttempt',0)for x in selected),mandatoryPrefixExcluded=sum(x.get('afterPositivePrefix',0)for x in selected),changingPages=sum(bool(x['pageChanged'])for x in selected)))
 new_positions=sorted((dict(pc=pc,hex=hex(pc),thumb=thumb,**v)for(pc,thumb),v in missing.items()if v.get('newToParked959:directRejectedAttempt',0)>0),key=lambda x:x.get('newToParked959:directRejectedAttempt',0),reverse=True)
 reports[action]={'newMissingPositions':len(new_positions),'newMissingDirectTotal':sum(x.get('newToParked959:directRejectedAttempt',0)for x in new_positions),'newMissingBudgets':[{'positions':n,'directRejectedAttempt':sum(x.get('newToParked959:directRejectedAttempt',0)for x in new_positions[:n]),'regions':len({(x['pc']&~1023,x['thumb'])for x in new_positions[:n]})}for n in [256,512,1024,2048,4096]],'newMissingRanked':new_positions,'totals':dict(totals),'rankedOutside':ranked,'budgets':budgets,'topMissingNew':sorted((dict(pc=pc,hex=hex(pc),thumb=thumb,**v)for(pc,thumb),v in missing.items()if any(k.startswith('newTo')for k in v)),key=lambda x:sum(v for k,v in x.items()if k.startswith('newTo')),reverse=True)[:30]}
report={'scope':'Exact existing-emitter syntactic support, unchanged opcodes across both observed windows, data-abort/no-fetch excluded. Outside-page positive-prefix steps are mandatory original-runtime fallback and excluded from direct addition opportunity. Missing-bit positive-prefix origin was not captured, so same-region unsupported vs cross-region cannot be distinguished; no automatic credit for it.','parkedAddedPositions':added,'parkedMaskPath':oldpath,'parkedMaskSha256':hashlib.sha256(Path(oldpath).read_bytes()).hexdigest(),'reports':reports}
(p/'cohorts.json').write_text(json.dumps(report,indent=2)+'\n')
for k,v in reports.items():print(k,json.dumps({'totals':v['totals'],'budgets':v['budgets'],'topNew':v['topMissingNew'][:3]}))
