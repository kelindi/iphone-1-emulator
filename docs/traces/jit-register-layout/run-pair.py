from pathlib import Path
import os,json,subprocess,time,hashlib
root=Path('.context/jit-register-layout');proof=json.loads(Path('.context/jit-instruction-guards/guest-proof.json').read_text());proof['seedSha256']='c6b855944ba9b1cd172a01668a994032be32c4a164dd86a5087a20cff0b05b19';expected=[r['hash'] for r in proof['checkpoints']];results=[]
for n,arm in enumerate(['baseline','candidate','candidate','baseline'],1):
 runtime=Path('.context/jit-register-layout/runtime/emu.wasm');assert hashlib.sha256(runtime.read_bytes()).hexdigest()==proof['runtimeSha256']
 seed=(Path('web/jit') if arm=='baseline' else root/'seed')/'seed.wasm';expected_seed='d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881' if arm=='baseline' else proof['seedSha256'];assert hashlib.sha256(seed.read_bytes()).hexdigest()==expected_seed
 env=dict(os.environ,REPLAY_MODULE=Path('.context/jit-register-layout/runtime/emu.js').resolve().as_uri(),REPLAY_JIT=('web/jit' if arm=='baseline' else str(root/'seed')),FIXTURE_SNAPSHOT='.context/graphics-speed/fixed-dismiss.snap',INSTRUCTIONS='600000000',REPLAY_SEEDED_ONLY='1',REPLAY_EXPECTED_HASHES=json.dumps(expected))
 log=root/('quiet-%s-%s.log'%(n,arm));err=log.with_suffix('.err');start=time.time()
 with log.open('w') as out,err.open('w') as error:subprocess.run(['node',str(root/'replay.mjs')],env=env,stdout=out,stderr=error,check=True)
 rows=[json.loads(s) for s in log.read_text().splitlines() if s.startswith('{')];checkpoints=[r for r in rows if r.get('enabled')==1]
 assert len(checkpoints)==6
 for row,ref in zip(checkpoints,proof['baseline']):
  for key in ['done','hash','tlb','generated','calls','regions']:assert row[key]==ref[key]
 result={'order':n,'arm':arm,'startUnix':start,'endUnix':time.time(),'runMs':checkpoints[-1]['runMs'],'checkpoints':checkpoints};results.append(result)
 print(json.dumps({k:v for k,v in result.items() if k!='checkpoints'}),flush=True)
 (root/'quiet-pair.json').write_text(json.dumps({'scope':'Quiet ABBA600M emu_run elapsed time, same runtime; full raw checkpoints required','results':results},indent=2)+'\n')
