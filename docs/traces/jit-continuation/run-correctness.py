from pathlib import Path
import json,subprocess,os
p=Path('.context/jit-continuation');reports=[];steps=os.environ.get('CONTINUATION_STEPS','100000000');tag=str(int(steps)//1000000)+'m'
for name,fixture in [('mbx','.context/graphics-speed/fixed-dismiss.snap'),('software','.context/graphics-speed/web-clipped-control/firmware/software-home.snap')]:
 reference=[j for l in Path('.context/jit-instruction-guards/'+name+'-original.log').read_text().splitlines() if l.startswith('{') for j in [json.loads(l)] if j.get('enabled')==1]
 env=dict(os.environ,REPLAY_MODULE=(p/'dist/emu.js').resolve().as_uri(),REPLAY_JIT='web/jit',FIXTURE_SNAPSHOT=fixture,REPLAY_REFERENCE=json.dumps(reference),INSTRUCTIONS=steps)
 with (p/(name+'-'+tag+'.log')).open('w') as f,(p/(name+'-'+tag+'.err')).open('w') as err:r=subprocess.run(['node',str(p/'replay.mjs')],env=env,stdout=f,stderr=err)
 print(name,r.returncode,flush=True);assert r.returncode==0,name
