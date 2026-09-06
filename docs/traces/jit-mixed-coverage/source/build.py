from pathlib import Path
import json,subprocess,hashlib,shutil
p=Path('.context/jit-mixed-coverage').resolve();root=Path.cwd();base=Path('.context/jit-fallback-census/build-report.json');receipt=json.loads(base.read_text());link=receipt['commands'][0]
assert hashlib.sha256(Path(receipt['originalArchive']).read_bytes()).hexdigest()==receipt['originalArchiveSha256']
(p/'original/dist').mkdir(exist_ok=True);(p/'candidate/dist').mkdir(exist_ok=True)
for name in ['emu.js','emu.wasm','package.json']:shutil.copy2(root/'.context/jit-fallback-census/control'/name,p/'original/dist'/name)
cmd=[str(root/'.context/emsdk/upstream/emscripten/emcc'),'-O3','-DNDEBUG','-DS5LBOX_WASM_JIT=1','-I'+str(root/'core/include'),'-I'+str(p/'candidate'),'-Wall','-Wextra','-std=gnu11','-c',str(p/'candidate/jit-runtime.c'),'-o',str(p/'candidate/jit-runtime.c.o')]
subprocess.run(cmd,check=True);candidate=link.copy();candidate[candidate.index('-o')+1]=str(p/'candidate/dist/emu.js');candidate=[str(p/'candidate/jit-runtime.c.o')if x.endswith('/jit-runtime.c.o')else x for x in candidate];subprocess.run(candidate,check=True);(p/'candidate/dist/package.json').write_text('{"type":"commonjs"}\n')
(p/'build.json').write_text(json.dumps({'commands':[cmd,candidate],'originalArchiveSha256':receipt['originalArchiveSha256'],'controlSha256':hashlib.sha256((p/'original/dist/emu.wasm').read_bytes()).hexdigest(),'candidateSha256':hashlib.sha256((p/'candidate/dist/emu.wasm').read_bytes()).hexdigest(),'scope':'Only REGION_CAP512→576 in original runtime; original RTC archive and original API. No census object or interpreter observer linked.'},indent=2)+'\n')
