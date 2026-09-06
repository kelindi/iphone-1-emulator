from pathlib import Path
import json,gzip,hashlib,copy
p=Path('.context/jit-mixed-coverage');c=Path('.context/jit-fallback-census')
load=lambda x:json.loads(Path(x).read_text())
cohorts=load(c/'cohorts.json')['reports']['warm-settings'];positions=json.loads(gzip.decompress((c/'emitter-positions.json.gz').read_bytes()))
lookup={(r['pc'],r['thumb']):r for r in positions}
original=json.loads(gzip.decompress((p/'original/seed-input.json.gz').read_bytes()));candidate=copy.deepcopy(original)
regions={(r['pc'],r['thumb']):r for r in candidate['regions']};observed_pages={}
for r in positions:
 if len(r['variants'])!=1:continue
 observed_pages.setdefault((r['pc']&~1023,r['thumb']),{})[r['pc']]=r['variants'][0]
chosen_pages=[]
for row in cohorts['rankedOutside']:
 if len(chosen_pages)==64:break
 if row['pageChanged']:continue
 key=(row['pc'],row['thumb']);assert key not in regions
 region={'pc':row['pc'],'thumb':row['thumb'],'index':512+len(chosen_pages),'ops':sorted(observed_pages[key].items())}
 candidate['regions'].append(region);regions[key]=region;chosen_pages.append(row)
assert len(chosen_pages)==64
chosen_positions=[];rejected=[]
for row in cohorts['newMissingRanked']:
 if len(chosen_positions)==2048:break
 key=(row['pc']&~1023,row['thumb']);assert key in regions and regions[key]['index']<512
 observed=lookup[(row['pc'],row['thumb'])];assert observed['emittable'] and len(observed['variants'])==1
 if observed['pageChanged']:rejected.append({'reason':'changing observed page',**row});continue
 old=dict(regions[key]['ops'])
 if row['pc']in old:
  rejected.append({'reason':'original input position already present; never replace original opcode','sameOpcode':old[row['pc']]==observed['variants'][0],**row});continue
 old[row['pc']]=observed['variants'][0];regions[key]['ops']=sorted(old.items());chosen_positions.append({**row,'opcode':observed['variants'][0]})
assert len(chosen_positions)==2048 and len(candidate['regions'])==576
# Never rewrite or remove any original captured instruction or definition.
for old in original['regions']:
 new=regions[(old['pc'],old['thumb'])];assert new['index']==old['index']
 assert all(dict(new['ops']).get(pc)==word for pc,word in old['ops'])
bytes=json.dumps(candidate,separators=(',',':')).encode();(p/'candidate/seed-input.json.gz').write_bytes(gzip.compress(bytes,mtime=0))
selected={'scope':'Actual direct rejected attempts, original emitter support and unchanged observed pages. Mandatory prefix fallback excluded from ranking credit; original bytes/regions/admissions never replaced.','pages':chosen_pages,'positions':chosen_positions,'rejected':rejected,'counts':{'pages':len(chosen_pages),'positions':len(chosen_positions),'modifiedOriginalRegions':len({(r['pc']&~1023,r['thumb'])for r in chosen_positions}),'outsideDirectUpper':sum(r.get('directRejectedAttempt',0)for r in chosen_pages),'newPositionDirectUpper':sum(r.get('newToParked959:directRejectedAttempt',0)for r in chosen_positions)},'budgets':{'regions':576,'maxSeedBytes':16*1024*1024,'maxBodyBytes':80*1024,'compileCPU':'matched browser control*1.25+100ms; rejection gate, not prediction; no optional loader timeout/fallback','unchangedGuestABIRevision':1},'pins':{str(x):hashlib.sha256(x.read_bytes()).hexdigest()for x in [c/'cohorts.json',c/'emitter-positions.json.gz',p/'original/seed-input.json.gz',p/'candidate/seed-input.json.gz']}}
(p/'selection.json').write_text(json.dumps(selected,indent=2)+'\n')
for src,dst,old,new in [('web/jit-runtime.c','jit-runtime.c','#define REGION_CAP 512u','#define REGION_CAP 576u'),('web/seed-jit.js','seed-jit.js','const SEED_MAX_REGIONS = 512;','const SEED_MAX_REGIONS = 576;')]:
 original=Path(src).read_text();assert original.count(old)==1;changed=original.replace(old,new)
 if dst=='seed-jit.js':changed=changed.replace('const SEED_MAX_BYTES = 32 * 1024 * 1024;','const SEED_MAX_BYTES = 16 * 1024 * 1024;')
 (p/'candidate'/dst).write_text(changed)
(p/'candidate/jit-runtime.h').write_bytes(Path('web/jit-runtime.h').read_bytes())
print(json.dumps(selected['counts']))
