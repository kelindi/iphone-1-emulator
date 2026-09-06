from pathlib import Path
import subprocess,json,hashlib,time,shlex,os
root=Path.cwd();p=root/'.context/web-lto';em=root/'.context/emsdk/upstream/emscripten';records=[]
def sha(f):return hashlib.sha256(Path(f).read_bytes()).hexdigest()
def run(cmd):
 records.append(cmd);(p/'commands.json').write_text(json.dumps(records,indent=2));subprocess.run(cmd,check=True)
pins=json.loads((p/'source-pins.json').read_text())
for n,h in pins['files'].items():assert sha(p/'source'/n)==h
for n,h in pins['production'].items():assert sha(root/n)==h
for kind in ['control','candidate']:
 build=p/(kind+'-build');dist=p/kind
 cmd=[str(em/'emcmake'),'cmake','-S',str(p/'source'),'-B',str(build),'-DIPHONE1_WEB=ON','-DIPHONE1_WASM_JIT=ON','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DIPHONE1_WEB_DIST='+str(dist)]
 if kind=='candidate':cmd+=['-DCMAKE_C_FLAGS=-flto=full']
 run(cmd);run(['cmake','--build',str(build),'--target','emu','--parallel','1'])
 if kind=='control':assert sha(dist/'emu.wasm')==pins['production']['web/dist/emu.wasm'],'full control build not byte-identical'
 original=sha(dist/'emu.wasm');originaljs=sha(dist/'emu.js')
 if kind=='candidate':
  # Re-run the exact link into the same isolated output; no object/flag changes.
  link=(build/'CMakeFiles/emu.dir/link.txt').read_text();subprocess.run(shlex.split(link),cwd=build,check=True);assert sha(dist/'emu.wasm')==original and sha(dist/'emu.js')==originaljs
for kind in ['control','candidate']:
 cmds=json.loads((p/(kind+'-build')/'compile_commands.json').read_text());assert len(cmds)==48
 for c in cmds:
  assert ('-flto=full'in c['command'])==(kind=='candidate')
  assert '-ffast-math'not in c['command'] and '-Ofast'not in c['command']
  if c['file'].endswith('/vfp.c'):assert '-ffp-contract=off'in c['command']
for n,h in pins['production'].items():assert sha(root/n)==h
out={'passed':True,'controlByteIdentical':True,'candidateRelinkByteIdentical':True,'compileUnits':48,'candidateFlagOnly':'-flto=full for all compile units and emu link; original -O3 and VFP -ffp-contract=off preserved','targetOnly':'emu','artifacts':{k:{'wasm':sha(p/k/'emu.wasm'),'js':sha(p/k/'emu.js'),'wasmBytes':(p/k/'emu.wasm').stat().st_size}for k in ['control','candidate']},'productionUnchanged':True}
(p/'build-report.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
