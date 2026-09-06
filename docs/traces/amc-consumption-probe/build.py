from pathlib import Path
import hashlib,json,subprocess,time
base=Path('.context/amc-first/stage-h');old=Path('.context/amc-first/stage-g')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
manifest=json.loads((old/'build.json').read_text())
for path,digest in manifest['pins'].items():assert sha(path)==digest,path
for name in ('amc_engine.c','amc_engine.h','amc_idle.c','amc_idle.h','amc_sram.c','amc_sram.h','amc_faad.c','emu_api.c'):
 assert (base/'source'/name).read_bytes()==(old/'source'/name).read_bytes(),name
common=['cc','-O2','-flto=1','-Wall','-Wextra','-Wno-misleading-indentation','-DAMC_INPUT_PREFIX=6','-I'+str(old/'native-include'),'-I'+str(old/'include'),'-I'+str(base/'source'),'-I.context/amc-first/stage-e/framing/source/include']
engine=['amc_engine.c','amc_faad.c','amc_idle.c','amc_sram.c','consume.c','peek.c']
lib=['.context/build-native/core/libemucore.a','.context/amc-first/stage-e/framing/build/libfaad.a','-lm']
reports=[]
for target,files,libraries in [('test_observer',['test_observer.c','consume.c'],[]),('test_peek',['test_peek.c','peek.c'],lib),('test_service',['test_service.c']+engine,lib),('probe',['probe.c']+engine,lib)]:
 command=common+[str(base/'source'/f) for f in files]+libraries+['-o',str(base/target)];start=time.monotonic()
 with (base/(target+'-build.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 if target!='probe':
  with (base/(target+'.log')).open('w') as log:subprocess.run([str(base/target)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
 reports.append({'target':target,'command':command,'sha256':sha(base/target),'elapsedSeconds':time.monotonic()-start,'guestExecuted':False})
with (base/'audit-test.log').open('w') as log:subprocess.run(['python3',str(base/'test_audit.py')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
source_hashes={str(p):sha(p) for directory in [base/'source',old/'include',old/'native-include'] for p in sorted(directory.rglob('*')) if p.is_file()}
refs=old/'run-1';reference_hashes={str(p):sha(p) for p in sorted(refs.rglob('*.bin')) if p.is_file() and not p.is_symlink() and not any(part.startswith('negative-') for part in p.relative_to(refs).parts)}
report={'passed':True,'scope':'Source builds, synthetic observer/retirement/peek/auditor contracts only. No StageH guest run.','threads':1,'builds':reports,'sources':source_hashes,'referenceFiles':reference_hashes,'libraryPins':manifest['pins'],'probeSHA256':sha(base/'probe')}
(base/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'probeSHA256':report['probeSHA256'],'targets':len(reports)}))
