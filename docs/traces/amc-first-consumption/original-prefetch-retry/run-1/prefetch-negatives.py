"""Invoke frozen positive auditor on cloned actual artifact sets; no guest runs."""
from pathlib import Path
import hashlib,json,subprocess,sys
p=Path(sys.argv[1]);script=p/'prefetch-audit.py';j=json.loads((p/'fetch.json').read_text());attempt=f"fetch-attempt-{j['count']}";cases=[]
def mutate(field):
 def f(b):
  j=json.loads(b)
  if field=='context':j['registers'][0]^=4
  if field=='availability':j['walks'][3]['available']=0
  if field=='mapping':j['walks'][3]['spans'][0][2]^=4096
  if field=='code':j['walks'][0]['codeHex']='01'+j['walks'][0]['codeHex'][2:]
  return json.dumps(j).encode()
 return f
for label,target,change,expected in [('retry-context',attempt+'/binding.json',mutate('context'),'actual mapped retry context differs'),('retry-availability',attempt+'/binding.json',mutate('availability'),'actual mapped original window differs'),('retry-mapping',attempt+'/binding.json',mutate('mapping'),'cloned physical translation differs'),('retry-code',attempt+'/binding-code.bin',lambda b:bytes([b[0]^1])+b[1:],'actual mapped original window differs')]:
 out=p/('negative-'+label);out.mkdir(exist_ok=False)
 for a in p.iterdir():
  if a.name.startswith('negative-') or a.name=='prefetch-audit.json':continue
  if a.name==attempt:
   (out/a.name).mkdir()
   for f in a.iterdir():
    dest=out/a.name/f.name
    if a.name+'/'+f.name==target:dest.write_bytes(change(f.read_bytes()))
    else:dest.symlink_to(f.resolve(),target_is_directory=f.is_dir())
  else:(out/a.name).symlink_to(a.resolve(),target_is_directory=a.is_dir())
 log=p/('negative-'+label+'.log')
 with log.open('w') as stream:r=subprocess.run([sys.executable,str(script),str(out)],stdout=stream,stderr=subprocess.STDOUT,timeout=30)
 assert r.returncode and expected in log.read_text(),label
 cases.append({'label':label,'returncode':r.returncode,'expectedFailure':expected,'logSHA256':hashlib.sha256(log.read_bytes()).hexdigest()})
report={'passed':True,'frozenAuditorSHA256':hashlib.sha256(script.read_bytes()).hexdigest(),'actualArtifactNegatives':cases}
(p/'prefetch-negatives.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
