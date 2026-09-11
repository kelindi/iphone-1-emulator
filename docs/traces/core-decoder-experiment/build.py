from pathlib import Path
import shutil,subprocess,json,hashlib
root=Path.cwd();p=root/'.context/core-hotspot';source=p/'source';source.mkdir(exist_ok=True)
for name in ['core','tools']:
 shutil.copytree(root/name,source/name,dirs_exist_ok=True)
(source/'web').mkdir(exist_ok=True)
for f in (root/'web').glob('*'):
 if f.is_file():shutil.copy2(f,source/'web'/f.name)
shutil.copy2(root/'CMakeLists.txt',source/'CMakeLists.txt')
commands=[]
def run(cmd):
 commands.append(cmd);(p/'build-commands.json').write_text(json.dumps(commands,indent=2));subprocess.run(cmd,check=True)
sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
for arm in ['control','candidate']:
 shutil.copy2(p/('original-arm_interp.c' if arm=='control' else 'arm_interp.c'),source/'core/src/arm/arm_interp.c')
 build=p/(arm+'-build');dist=p/arm
 run([str(root/'.context/emsdk/upstream/emscripten/emcmake'),'cmake','-S',str(source),'-B',str(build),'-DIPHONE1_WEB=ON','-DCMAKE_BUILD_TYPE=Release','-DIPHONE1_WEB_DIST='+str(dist)])
 run(['cmake','--build',str(build),'--target','emu','--parallel','2'])
 if arm=='control':assert sha(dist/'emu.wasm')==sha(root/'web/dist/emu.wasm'),'control differs'
(p/'build-result.json').write_text(json.dumps({'controlByteIdentical':True,'modules':{a:sha(p/a/'emu.wasm') for a in ['control','candidate']},'productionSha256':sha(root/'web/dist/emu.wasm')},indent=2))
