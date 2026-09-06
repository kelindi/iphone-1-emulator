"""Build/review helper; never runs the guest by default.

`--build` links and runs only wrapper contracts. `--run NEW_DIR` is the explicit
reviewed experiment, and must not be used until parent source review permits it.
No archived binaries or input payloads are copied to tracked documentation.
"""
from pathlib import Path
import argparse,hashlib,json,resource,shutil,subprocess,time
base=Path('.context/amc-first/stage-g')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
pins={
 '.context/audio/ringtone-page.snap':'385aead20010aaf796be93423dade4afb5dc13c824d59c7857a452f7fbafd2e6',
 'web/firmware/kernel_1A543a.macho':'42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9',
 '.context/amc-first/stage-d/verified/sram.bin':'011452fdff9699442c49115354c9b0bced1b98265ba64ee5b95c1c48bbb01583',
 '.context/amc-first/stage-d/verified/written.bin':'5f15b3ccf7ff3acdd8494c7deb8dc2dc550bc950c2e15abc58625c606f24dad9',
 '.context/build-native/core/libemucore.a':'0a02dbd345758710c8f635487abafd19b2b1597ea32fb2b1c60fd1526cd4aec2',
 '.context/amc-first/stage-e/framing/build/libfaad.a':'7a091924433488caa93806aa2017ba8428172878b13f5d2a0122a2e2d07f6866',
 '.context/amc-first/stage-g/source/emu_api.c':'87dd9c5c3ec3047e907935a2e61fb8a70d0975ec94184b50aa97d627646aaec1',
}
def sources():
 return {str(p):sha(p) for directory in ('source','include','native-include') for p in sorted((base/directory).rglob('*')) if p.is_file()}
def validate():
 for path,digest in pins.items():assert sha(path)==digest, path
 for name in ('amc_engine.c','amc_engine.h','amc_faad.c','amc_idle.c','amc_idle.h','amc_sram.c','amc_sram.h'):
  assert (base/'source'/name).read_bytes()==(Path('.context/amc-first/stage-f/run-4/source')/name).read_bytes(), name
p=argparse.ArgumentParser();p.add_argument('--build',action='store_true');p.add_argument('--run',type=Path);args=p.parse_args()
if not args.build and not args.run:
 print('Prepared only; use --build for native wrapper checks, --run NEW_DIR only after source review.');raise SystemExit(0)
validate()
if args.build:
 commands=[]
 for name,source in [('probe','probe.c'),('test_hook','test_hook.c')]:
  command=['cc','-O2','-Wall','-Wextra','-Wno-misleading-indentation','-DAMC_INPUT_PREFIX=6','-I'+str(base/'native-include'),'-I'+str(base/'include'),'-I'+str(base/'source'),'-I.context/amc-first/stage-e/framing/source/include']
  command += [str(base/'source'/f) for f in (source,'amc_engine.c','amc_faad.c','amc_idle.c','amc_sram.c')]
  command += ['.context/build-native/core/libemucore.a','.context/amc-first/stage-e/framing/build/libfaad.a','-lm','-o',str(base/name)]
  commands.append(command)
  with (base/(name+'-build.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
 with (base/'hook-test.log').open('w') as log:subprocess.run([str(base/'test_hook')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
 (base/'build.json').write_text(json.dumps({'commands':commands,'sources':sources(),'pins':pins,'probeSHA256':sha(base/'probe'),'hookSHA256':sha(base/'test_hook')},indent=2)+'\n')
if args.run:
 manifest=json.loads((base/'build.json').read_text());assert manifest['sources']==sources();assert manifest['probeSHA256']==sha(base/'probe')
 out=args.run;out.mkdir(parents=True,exist_ok=False);(out/'upload').mkdir()
 shutil.copytree(base/'source',out/'source');shutil.copy2(base/'build.json',out/'build.json')
 shutil.copy2(base/'audit.py',out/'audit.py');shutil.copy2(__file__,out/'launch.py')
 shutil.copy2('.context/amc-first/stage-d/source/kernel-oracle.py',out/'kernel-oracle.py')
 command=[str(base/'probe'),'.context/audio/ringtone-page.snap','1',str(out),'.context/amc-first/stage-d/verified/sram.bin','.context/amc-first/stage-d/verified/written.bin']
 limits={'wallSeconds':240,'cpuSeconds':180,'addressSpaceBytes':1024**3,'perFileBytes':8*1024**2,'totalRetiredInstructions':155300000,'preInputInstructionsAfterStart':5000000,'postInputInstructions':5000000,'publicationStop':1,'hardPublicationCap':2}
 def restrict():
  resource.setrlimit(resource.RLIMIT_CPU,(180,180));resource.setrlimit(resource.RLIMIT_AS,(1024**3,1024**3));resource.setrlimit(resource.RLIMIT_FSIZE,(8*1024**2,8*1024**2));resource.setrlimit(resource.RLIMIT_CORE,(0,0))
 start=time.monotonic();code=None;timed_out=False
 with (out/'trace.log').open('w') as log:
  try:code=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,preexec_fn=restrict,timeout=240).returncode
  except subprocess.TimeoutExpired:timed_out=True
 report={'command':command,'limits':limits,'returncode':code,'timedOut':timed_out,'wallSeconds':time.monotonic()-start,'guestPass':code==0,'externalAuditsPass':False,'pins':pins,'auditSHA256':sha(out/'audit.py'),'kernelOracleSHA256':sha(out/'kernel-oracle.py'),'launcherSHA256':sha(out/'launch.py')}
 (out/'run.json').write_text(json.dumps(report,indent=2)+'\n')
 # Earlier prefixes are preserved even when the target is not reached.
 if (out/'upload/sram.bin').exists():
  shutil.copy2(out/'trace.log',out/'upload/trace.log')
  with (out/'upload/kernel-oracle.log').open('w') as log:
   subprocess.run(['python3',str(out/'kernel-oracle.py'),str(out/'upload')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 if code==0:
  with (out/'functional-audit.log').open('w') as log:
   subprocess.run(['python3',str(out/'audit.py'),str(out)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  negatives=[]
  original_artifacts=[p for p in out.iterdir() if p.name!='functional-audit.json']
  for label,target,message in [('input','input-final.bin','input-final.bin'),('pcm','first-pcm.bin','PCM differs from independent packetized decode')]:
   mutant=out/('negative-'+label);mutant.mkdir()
   for artifact in original_artifacts:
    if artifact.name==target:
     data=bytearray(artifact.read_bytes());data[123]^=1;(mutant/target).write_bytes(data)
    else:(mutant/artifact.name).symlink_to(artifact.resolve(),target_is_directory=artifact.is_dir())
   log_path=out/('negative-'+label+'.log')
   with log_path.open('w') as log:
    rejected=subprocess.run(['python3',str(out/'audit.py'),str(mutant)],stdout=log,stderr=subprocess.STDOUT,timeout=30)
   assert rejected.returncode!=0 and message in log_path.read_text(), 'artifact mutant did not fail the intended audit'
   negatives.append({'target':target,'xorOffset':123,'returncode':rejected.returncode,'log':str(log_path),'expectedFailure':message})
  report['artifactNegativeControls']=negatives
  report['externalAuditsPass']=True
  (out/'run.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
 if code!=0:raise SystemExit(10)
