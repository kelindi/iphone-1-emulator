import gzip,hashlib,json,os,subprocess,tempfile,time,sys
from pathlib import Path
from snapshot_transport import receive_fifo,publish
B=Path(__file__).resolve().parent
results=[]
with tempfile.TemporaryDirectory(prefix='guest-http-transport-')as temp:
 d=Path(temp)
 for mode in ['positive','truncated_writer','reader_failure','no_writer']:
  fifo=d/(mode+'.fifo');os.mkfifo(fifo,0o600);partial=d/(mode+'.partial');target=d/(mode+'.snap.gz')
  command=[str(B/'test_snapshot_transport'),'emit',str(fifo)]+(['truncate']if mode=='truncated_writer' else [])
  if mode=='no_writer':command=[sys.executable,'-c','raise SystemExit(7)']
  proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE);end=time.monotonic()+15;receipt=None;failure=None
  try:receipt=receive_fifo(fifo,partial,proc,lambda:end,fail_after=50 if mode=='reader_failure'else None)
  except Exception as error:failure=str(error)
  try:stdout,stderr=proc.communicate(timeout=5)
  except subprocess.TimeoutExpired:proc.kill();stdout,stderr=proc.communicate();raise AssertionError('writer deadlocked '+mode)
  if mode=='positive':
   assert proc.returncode==0 and receipt and failure is None
   raw=gzip.decompress(partial.read_bytes());verify=subprocess.run([str(B/'test_snapshot_transport'),'verify'],input=raw,capture_output=True,timeout=15);assert verify.returncode==0
   original=stderr.decode().split('RAW ',1)[1].strip();again=verify.stderr.decode().split('VERIFY status=0 ',1)[1].strip();assert original==again
   assert len(raw)==receipt['rawBytes'] and hashlib.sha256(raw).hexdigest()==receipt['rawSHA256']
   for writer_ok,gate in [(False,True),(True,False)]:
    try:publish(partial,target,receipt,writer_ok,gate)
    except ValueError:pass
    else:raise AssertionError('unqualified publication accepted')
    assert not target.exists() and partial.exists()
   publish(partial,target,receipt,True,True);assert target.exists()and not partial.exists()
   # The actual original loader rejects truncated/corrupt standard streams.
   damaged=bytearray(raw);damaged[-9]^=1
   for mutant in [raw[:-1],bytes(damaged),raw+b'x']:
    check=subprocess.run([str(B/'test_snapshot_transport'),'verify'],input=mutant,capture_output=True,timeout=15);assert check.returncode!=0
  else:assert failure and not target.exists() and proc.returncode!=0
  results.append({'case':mode,'passed':True,'writerReturncode':proc.returncode,'error':failure,'receipt':receipt,'writerStderr':stderr.decode()})
(B/'transport-test.json').write_text(json.dumps({'passed':True,'cases':results,'scope':'Real standard M68 snapshot through FIFO/gzip and original native loader; no guest boot.'},indent=2)+'\n');print('PASS4 transport cases,2 publication refusals,3 original-loader corruption refusals; exact standard snapshot hash/size conserved')
