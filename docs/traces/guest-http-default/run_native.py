"""One private native boot, operator-reviewed normal capture, bounded transport."""
import datetime,gzip,hashlib,json,os,signal,subprocess,threading,time
from pathlib import Path
from snapshot_transport import receive_fifo,publish
B=Path(__file__).resolve().parent;R=B/'native-run-1';R.mkdir(exist_ok=False)
pins=json.loads((B/'source-pins.json').read_text())
for p,h in pins.items():
 digest=hashlib.sha256()
 with Path(p).open('rb')as f:
  for c in iter(lambda:f.read(1024*1024),b''):digest.update(c)
 assert digest.hexdigest()==h,p
fifo=R/'snapshot.fifo';os.mkfifo(fifo,0o600);partial=R/'listener.snap.gz.partial';target=R/'listener.snap.gz'
cmd=[str(B/'probe'),'--board','m68','--kernel','web/firmware/kernel_1A543a.macho','--devicetree','web/firmware/devicetree_m68ap.bin','--ramdisk',str(B/'installed/default-local-demo.img'),'--ram-mb','256','--debug-enabled','--cmdline','debug=0x8 serial=3 rd=md0 -v','--steps','10000000000','--serial',str(R/'serial.log'),'--screenshot',str(R/'frame'),'--shot-every','500000000']
for pc in json.loads((B/'watch-pcs.json').read_text()):cmd+=['--watch-pc',pc]
state={'captureAt':None};start=time.monotonic();report={'startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'command':cmd,'instructionCap':10000000000,'bootWallCapSeconds':900,'serializationCapSeconds':120,'sourcePinsSHA256':hashlib.sha256((B/'source-pins.json').read_bytes()).hexdigest(),'passed':False};transport={}
with (R/'stdout.log').open('wb')as out,(R/'stderr.log').open('wb')as err:
 env=os.environ.copy();env['OWN_CAPTURE_PIPE']=str(fifo);proc=subprocess.Popen(cmd,stdout=out,stderr=err,env=env);report['pid']=proc.pid;(R/'running.json').write_text(json.dumps(report,indent=2)+'\n')
 def deadline():return state['captureAt']+120 if state['captureAt']is not None else start+900
 def receiver():
  try:transport['receipt']=receive_fifo(fifo,partial,proc,deadline)
  except Exception as e:transport['error']=repr(e)
 thread=threading.Thread(target=receiver,daemon=True);thread.start()
 try:
  while proc.poll() is None:
   if time.monotonic()>deadline():report['timeoutStage']='serialization'if state['captureAt']else'boot';proc.terminate();break
   free=os.statvfs(R);available=free.f_bavail*free.f_frsize
   if available<4300000000:report['diskFloorStop']=available;proc.terminate();break
   request=R/'capture-request.json'
   if state['captureAt']is None and request.exists():
    review=json.loads(request.read_text());frame=Path(review['frame']).resolve();assert frame.parent==R and frame.name.startswith('frame.') and frame.is_file();assert hashlib.sha256(frame.read_bytes()).hexdigest()==review['sha256'];report['reviewedFrame']=review;state['captureAt']=time.monotonic();os.kill(proc.pid,signal.SIGUSR1)
   if 'error' in transport:raise RuntimeError('FIFO reader failed: '+transport['error'])
   time.sleep(.1)
 except BaseException as error:
  report['launcherError']=repr(error)
  if proc.poll() is None:proc.terminate()
 try:proc.wait(timeout=10)
 except subprocess.TimeoutExpired:proc.kill();proc.wait();report['forcedKill']=True
 thread.join(timeout=5)
 if thread.is_alive():report['receiverJoinFailure']=True
 report['returncode']=proc.returncode
report['elapsedSeconds']=time.monotonic()-start;report['transport']=transport
log=(R/'stdout.log').read_text(errors='replace')
report['listenerGate']='HTTP final ready=1 seen=1 entries=1 bind=1/1 listen=1/1 written=1 uart=1 failed=0 'in log
report['normalCapture']='reason=own-http-reviewed-home' in log and 'pipe_ok=1' in log and 'reviewedFrame' in report
if proc.returncode==0 and report['listenerGate'] and report['normalCapture'] and 'receipt'in transport and not thread.is_alive():
 # Actual original loader checks the expanded standard stream, without raw file duplication.
 with (R/'verify-stdout.log').open('wb')as out,(R/'verify-stderr.log').open('wb')as err:
  verifier=subprocess.Popen([str(B/'test_snapshot_transport'),'verify'],stdin=subprocess.PIPE,stdout=out,stderr=err)
  feed_errors=[]
  def feed():
   try:
    with gzip.open(partial,'rb')as expanded:
     for chunk in iter(lambda:expanded.read(1024*1024),b''):verifier.stdin.write(chunk)
    verifier.stdin.close()
   except Exception as error:feed_errors.append(repr(error))
  feeder=threading.Thread(target=feed,daemon=True);feeder.start()
  try:verifier.wait(timeout=30)
  except subprocess.TimeoutExpired:report['loaderError']='30-second whole-feed/verify timeout';verifier.kill();verifier.wait()
  feeder.join(timeout=5)
  if feed_errors or feeder.is_alive():report['loaderError']={'feedErrors':feed_errors,'threadAlive':feeder.is_alive()}
 report['originalLoaderReturncode']=verifier.returncode
 if verifier.returncode==0 and 'loaderError' not in report:
  publish(partial,target,transport['receipt'],True,True);report['passed']=True
(R/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
