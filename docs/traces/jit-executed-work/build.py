from pathlib import Path
import subprocess,shlex,json,hashlib,shutil
root=Path.cwd();p=root/'.context/jit-executed-work';build=root/'.context/rtc-write/web-build';em=root/'.context/emsdk/upstream/emscripten'
def sha(f):return hashlib.sha256(Path(f).read_bytes()).hexdigest()
raw=shlex.split((build/'CMakeFiles/emu.dir/link.txt').read_text())
link=[]
for x in raw:
    link.extend(shlex.split((build/x[1:]).read_text()) if x.startswith('@') else [x])
link=[str(build/x) if x.endswith('.o') or x=='core/libemucore.a' else x for x in link]
archive=next(x for x in link if x.endswith('/libemucore.a'));runtime=next(x for x in link if x.endswith('/jit-runtime.c.o'))
assert sha(root/'.context/rtc-write/web-source/web/dist/emu.wasm')==sha(root/'web/dist/emu.wasm')=='7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088'
records=[]
def run(cmd):records.append(cmd);subprocess.run(cmd,check=True)
for name in ['control','observer']:(p/name).mkdir(exist_ok=True)
# Freeze base archive and exact shipped frontend before any new link.
shutil.copy2(archive,p/'base-libemucore.a');shutil.copy2(root/'web/jit-runtime.c',p/'runtime-baseline.c');shutil.copy2(root/'web/jit-runtime.h',p/'jit-runtime.h')
for file in ['index.html','app.js','phone.css','emu-worker.js','seed-jit.js','storage.js','checkpoint-worker.js','manifest.json']:
 q=p/'frozen-web'/file;q.parent.mkdir(exist_ok=True);
 if not q.exists():shutil.copy2(root/'web'/file,q)
ctl=link.copy();ctl[ctl.index('-o')+1]=str(p/'control/emu.js');ctl=[str(p/'base-libemucore.a') if x==archive else x for x in ctl];run(ctl)
assert sha(p/'control/emu.wasm')==sha(root/'web/dist/emu.wasm'),'no-observer relink differs'
base=[str(em/'emcc'),'-DS5LBOX_WASM_JIT=1','-I'+str(root/'core/include'),'-I'+str(p),'-O3','-DNDEBUG','-Wall','-Wextra','-std=gnu11','-c']
for src,obj in [('arm_interp.c','arm_interp.c.o'),('jit-runtime.c','jit-runtime.c.o'),('census.c','census.c.o')]:run(base+[str(p/src),'-o',str(p/obj)])
shutil.copy2(p/'base-libemucore.a',p/'observer-libemucore.a');run([str(em/'emar'),'r',str(p/'observer-libemucore.a'),str(p/'arm_interp.c.o')])
obs=link.copy();obs[obs.index('-o')+1]=str(p/'observer/emu.js');obs=[str(p/'observer-libemucore.a') if x==archive else str(p/'jit-runtime.c.o') if x==runtime else x for x in obs];obs.append(str(p/'census.c.o'));run(obs)
for n in ['control','observer']:(p/n/'package.json').write_text('{"type":"commonjs"}\n')
(p/'build-report.json').write_text(json.dumps(dict(commands=records,originalArchive=str(archive),originalArchiveSha256=sha(archive),frozenArchiveSha256=sha(p/'base-libemucore.a'),productionSha256=sha(root/'web/dist/emu.wasm'),controlSha256=sha(p/'control/emu.wasm'),observerSha256=sha(p/'observer/emu.wasm'),controlIdentical=True),indent=2)+'\n')
