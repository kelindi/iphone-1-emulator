from pathlib import Path
import subprocess,shutil,shlex,json,hashlib,os
r=Path.cwd(); b=r/'.context/safari-default-mbx'; w=b/'release-wasm'; w.mkdir(exist_ok=True)
em=r/'.context/emsdk/upstream/emscripten'; build=r/'web/build'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(args,cwd=None):
 print(shlex.join(map(str,args)),flush=True); subprocess.run(list(map(str,args)),cwd=cwd,check=True)
assert (r/'core/src/soc/mbx.c').read_bytes()==(b/'baseline.c').read_bytes()
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands'],text=True)
(w/'original-commands.txt').write_text(commands)
link=shlex.split(next(x for x in commands.splitlines() if '-sEXPORTED_FUNCTIONS=' in x))[2:-2]
compile=shlex.split(next(x for x in commands.splitlines() if x.endswith('/core/src/soc/mbx.c')))
# Preserve all code generation flags; replace only source/object/dependency outputs.
for flag,suffix in [('-o','mbx.c.o'),('-MF','mbx.c.o.d'),('-MT','mbx.c.o')]:compile[compile.index(flag)+1]=str(w/suffix)
compile[-1]=str(b/'candidate.c'); run(compile,build)
shutil.copyfile(build/'core/libemucore.a',w/'baseline.a');shutil.copyfile(w/'baseline.a',w/'candidate.a')
run([em/'emar','r',w/'candidate.a',w/'mbx.c.o'])
def members(p):
 names=subprocess.check_output([str(em/'emar'),'t',str(p)],text=True).splitlines()
 assert len(names)==len(set(names))
 return {n:hashlib.sha256(subprocess.check_output([str(em/'emar'),'p',str(p),n])).hexdigest() for n in names}
a=members(w/'baseline.a');c=members(w/'candidate.a'); assert a.keys()==c.keys(); changed=[n for n in a if a[n]!=c[n]];assert changed==['mbx.c.o'],changed
for name in ['baseline','candidate']:
 out=w/name;out.mkdir(exist_ok=True)
 args=[str(w/(name+'.a')) if x=='core/libemucore.a' else str(out/'emu.js') if x=='../dist/emu.js' else x for x in link]
 run(args,build)
assert sha(w/'baseline/emu.wasm')==sha(r/'web/dist/emu.wasm'),'Baseline exact-release relink mismatch; STOP'
# Genuine portable MBX tests compiled to WASM against the same core archive/runtime.
run([em/'emcc','-O3','-DNDEBUG','-DS5LBOX_WASM_JIT=1','-I'+str(r/'core/include'),b/'test_mbx.c',build/'CMakeFiles/emu.dir/web/jit-runtime.c.o',w/'candidate.a','-sALLOW_TABLE_GROWTH=1','-sALLOW_MEMORY_GROWTH=1','-sINITIAL_MEMORY=536870912','-sSTACK_SIZE=4194304','-sENVIRONMENT=node','-o',w/'test_mbx.js'])
with (w/'test_mbx.log').open('w') as f:subprocess.run(['node',str(w/'test_mbx.js')],stdout=f,stderr=subprocess.STDOUT,check=True)
out=b/'web';assert not out.exists()
shutil.copytree(r/'web',out,ignore=shutil.ignore_patterns('build','tests','node_modules'),copy_function=lambda s,d:os.link(s,d) if '/firmware/' in str(s) else shutil.copyfile(s,d))
for n in ['emu.js','emu.wasm']:shutil.copyfile(w/'candidate'/n,out/'dist'/n)
pins={str(p.relative_to(r)):sha(p) for p in [r/'web/dist/emu.wasm',w/'candidate/emu.wasm',r/'web/emu_api.c',r/'web/jit-runtime.c',build/'CMakeFiles/emu.dir/web/emu_api.c.o',build/'CMakeFiles/emu.dir/web/jit-runtime.c.o',b/'candidate.c']}
for p in (r/'web/jit').iterdir():
 if p.is_file():assert sha(p)==sha(out/'jit'/p.name);pins[str(p.relative_to(r))]=sha(p)
report=dict(passed=True,productionWasmBaselineRelinkExact=True,changedArchiveMembers=changed,archiveMembers=a,candidateArchiveMembers=c,pins=pins,originalLinkCommand=link,candidateCompileCommand=compile,tests=(w/'test_mbx.log').read_text())
(w/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k not in ['archiveMembers','candidateArchiveMembers']},indent=2))
