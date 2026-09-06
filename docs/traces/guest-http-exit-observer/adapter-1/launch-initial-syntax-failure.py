"""Prepared ONE-attempt diagnostic launcher. Not invoked during preparation.
Pins and exact original snapshot are checked before any guest instructions.
"""
from pathlib import Path
import gzip,hashlib,json,os,subprocess,sys,threading,time
B=Path(__file__).resolve().parent
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb')as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def main():
 if len(sys.argv)!=2:raise SystemExit('usage: launch.py NEW_ATTEMPT_DIRECTORY (requires prior reviewed scheduling)')
 out=Path(sys.argv[1]).resolve();assert not out.exists()
 pins=json.loads((B/'pins.json').read_text());assert all(sha(p)==h for p,h in pins['files'].items())
 available=os.statvfs(B);assert available.f_bavail*available.f_frsize>=4500000000
 # Runner creates its own fresh output dir, refusing an existing attempt.
 snapshot=B.parent/'native-run-1/listener.snap.gz';start=time.monotonic()
 cmd=[str(B/'runner'),str(snapshot),str(out),str(B/'references')]
 log=B/(out.name+'-launcher.log');assert not log.exists()
 receipt={'command':cmd,'pinsSHA256':sha(B/'pins.json'),'scope':'Native diagnostic; not browser replay, no default promotion','caps':{'instructions':8000000000,'runWallSeconds':450,'serializerSeconds':120,'outerProcessSeconds':580},'passed':False}
 with log.open('xb')as f:
  p=subprocess.Popen(cmd,stdout=f,stderr=f)
  try:p.wait(timeout=580)
  except subprocess.TimeoutExpired:
   receipt['outerTimeout']=True;p.terminate()
   try:p.wait(timeout=5)
   except subprocess.TimeoutExpired:p.kill();p.wait()
 receipt['returncode']=p.returncode;receipt['elapsedSeconds']=time.monotonic()-start
 partial=out/'boundary.snap.gz.partial';native=out/'report.json'
 if p.returncode==0 and native.exists() and partial.exists():
  d=json.loads(native.read_text());receipt['native']=d
  assert d['normalOutsideRunStop'] and d['snapshotPartialComplete'] and d['additionalInstructions']<=8000001024 and d['runSeconds']<451
  raw=hashlib.sha256();size=0;feed_error=[]
  with (out/'snapshot-verify.log').open('wb')as vf:
   v=subprocess.Popen([str(B.parent/'test_snapshot_transport'),'verify'],stdin=subprocess.PIPE,stdout=vf,stderr=vf)
   def feed():
    nonlocal size
    try:
     with gzip.open(partial,'rb')as g:
      for b in iter(lambda:g.read(1048576),b''):
       size+=len(b);assert size<=512*1024*1024;raw.update(b);v.stdin.write(b)
     v.stdin.close()
    except BaseException as e:feed_error.append(repr(e))
   t=threading.Thread(target=feed,daemon=True);t.start()
   try:v.wait(timeout=30)
   except subprocess.TimeoutExpired:v.kill();v.wait();feed_error.append('validator30s cap')
   t.join(2)
  receipt['snapshot']={'rawBytes':size,'rawSHA256':raw.hexdigest(),'gzipBytes':partial.stat().st_size,'gzipSHA256':sha(partial),'validatorReturncode':v.returncode,'feedErrors':feed_error,'threadAlive':t.is_alive()}
  if v.returncode==0 and not feed_error and not t.is_alive():
   os.rename(partial,out/'boundary.snap.gz');receipt['snapshot']['publishedAsDiagnosticEvidenceOnly']=True
  receipt['diagnosticExitStoreWitness']=d['reason']=='observed-exit-status-store'
 # No script marks an app milestone or daemon-cause claim from snapshot existence.
 receipt['postPinsExact']=all(sha(p)==h for p,h in pins['files'].items())
 target=out/'launcher-report.json'if out.exists()else B/(out.name+'-setup-failure.json');target.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
 return 0 if receipt.get('snapshot',{}).get('publishedAsDiagnosticEvidenceOnly')else1
if __name__=='__main__':raise SystemExit(main())
