from pathlib import Path
import subprocess,shlex,json,hashlib
r=Path.cwd();p=r/'.context/connected-engine';em=r/'.context/emsdk/upstream/emscripten/emcc';base=r/'.context/core-hotspot/control-build';src=Path('web/jit-runtime.c').read_text()
helper='''
/* Isolated connected-code import. Locals remain in generated code; this helper
 * only refreshes a successful FETCH mapping and checks destination admission.
 * It never walks page tables, invokes guest code, or commits register state. */
EMSCRIPTEN_KEEPALIVE uintptr_t emu_jit_connected_fetch(arm_cpu_t *c, uint32_t pc,
                                                      uint32_t flags) {
    if (!bound_machine || c != &bound_machine->cpu || !arm_wasm_jit_enabled ||
        c->abort_pending || !arm_mode_is_valid(flags) ||
        ((c->cpsr ^ flags) & (ARM_CPSR_MODE_MASK | ARM_CPSR_I | ARM_CPSR_F)) ||
        (c->irq_line && !(flags & ARM_CPSR_I)) ||
        (c->fiq_line && !(flags & ARM_CPSR_F))) return 0;
    const unsigned thumb = (flags & ARM_CPSR_T) != 0;
    if (pc & (thumb ? 1u : 3u)) return 0;
    region_t *r = region_find(pc, thumb, false);
    unsigned bit = (pc & (REGION_BYTES - 1u)) >> (thumb ? 1 : 2);
    if (!r || !(r->supported[bit >> 5] & (1u << (bit & 31u)))) return 0;
    if (!arm_fetch_cache_try_refill(c, pc, (flags & ARM_CPSR_MODE_MASK) != ARM_MODE_USR))
        return 0;
    return (uintptr_t)c->fetch_host;
}
'''
(p/'jit-runtime.c').write_text(src+helper);(p/'runtime').mkdir(exist_ok=True)
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
