from pathlib import Path
import subprocess,shlex,json,hashlib
root=Path.cwd();p=root/'.context/jit-continuation';em=root/'.context/emsdk/upstream/emscripten'
commands=subprocess.check_output(['ninja','-C','web/build','-t','commands','emu'],text=True).splitlines();link=shlex.split(commands[-1].split('&&')[1].strip())
link=[str(root/'web/build'/x) if x.endswith('.o') or x=='core/libemucore.a' else x for x in link]
original=next(x for x in link if x.endswith('/jit-runtime.c.o'))
compile=[str(em/'emcc'),'-DS5LBOX_WASM_JIT=1','-I'+str(root/'core/include'),'-O3','-DNDEBUG','-Wall','-Wextra','-std=gnu11','-c',str(p/'jit-runtime.c'),'-o',str(p/'jit-runtime.c.o')]
subprocess.run(compile,check=True)
link[link.index('-o')+1]=str(p/'dist/emu.js');link=[str(p/'jit-runtime.c.o') if x==original else x for x in link]
subprocess.run(link,check=True);(p/'dist/package.json').write_text('{"type":"commonjs"}\n')
(p/'build-provenance.json').write_text(json.dumps({'compile':compile,'link':link,'productionWasmSha256':hashlib.sha256((root/'web/dist/emu.wasm').read_bytes()).hexdigest(),'candidateWasmSha256':hashlib.sha256((p/'dist/emu.wasm').read_bytes()).hexdigest(),'productionArchiveSha256':hashlib.sha256((root/'web/build/core/libemucore.a').read_bytes()).hexdigest()},indent=2)+'\n')
