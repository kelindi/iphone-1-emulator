from pathlib import Path
import subprocess,shlex,json,hashlib
r=Path.cwd();p=r/'.context/exit-census';em=r/'.context/emsdk/upstream/emscripten/emcc';base=r/'.context/core-hotspot/control-build';src=(p/'jit-runtime-source.c').read_text()
(p/'jit-runtime.c').write_text(src);(p/'runtime').mkdir(exist_ok=True)
commands=[]
def run(cmd,cwd=r):commands.append([str(x)for x in cmd]);subprocess.run(cmd,cwd=cwd,check=True)
obj=p/'jit-runtime.c.o';run([em,'-O3','-DNDEBUG','-DS5LBOX_WASM_JIT=1','-std=gnu11','-I'+str(r/'web'),'-I'+str(r/'core/include'),'-c',p/'jit-runtime.c','-o',obj])
args=shlex.split((base/'CMakeFiles/emu.dir/link.txt').read_text());out=[]
for arg in args:
 if arg=='@CMakeFiles/emu.dir/objects1.rsp':out.extend([str(base/'CMakeFiles/emu.dir/web/emu_api.c.o'),str(obj)])
 elif arg=='@CMakeFiles/emu.dir/linklibs.rsp':out.extend([str(base/'core/libemucore.a'),'-lm'])
 elif arg==str(r/'.context/core-hotspot/control/emu.js'):out.append(str(p/'runtime/emu.js'))
 else:out.append(arg)
run(out);(p/'runtime/package.json').write_text('{"type":"commonjs"}\n')
(p/'runtime-build.json').write_text(json.dumps({'commands':commands,'productionSha256':hashlib.sha256(Path('web/dist/emu.wasm').read_bytes()).hexdigest(),'runtimeSha256':hashlib.sha256((p/'runtime/emu.wasm').read_bytes()).hexdigest(),'sharedCoreArchive':str(base/'core/libemucore.a'),'sharedCoreSha256':hashlib.sha256((base/'core/libemucore.a').read_bytes()).hexdigest()},indent=2))
