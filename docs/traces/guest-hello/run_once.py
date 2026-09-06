"""One approved normal boot. No automatic retry, restoration or network model."""
import hashlib,json,subprocess,time
from pathlib import Path
B=Path(__file__).resolve().parent
run=B/'run-1';run.mkdir(exist_ok=False)
pins=json.loads((B/'source-pins.json').read_text())
for path,expected in pins.items():assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==expected,path
cmd=[str(B/'probe'),'--board','m68','--kernel','web/firmware/kernel_1A543a.macho','--devicetree','web/firmware/devicetree_m68ap.bin','--ramdisk',str(B/'installed/hello.img'),'--ram-mb','256','--debug-enabled','--cmdline','debug=0x8 serial=3 rd=md0 -v','--steps','10000000000','--serial',str(run/'serial.log'),'--screenshot',str(run/'final.ppm')]
for pc in ['0x1f04','0x1f4c','0x1fa0','0x30009860','0x300099f0']:cmd+=['--watch-pc',pc]
start=time.monotonic();report={'command':cmd,'instructionCap':10000000000,'wallCapSeconds':900,'sourcePinsSHA256':hashlib.sha256((B/'source-pins.json').read_bytes()).hexdigest(),'scope':'One private normal boot; functional CPU census may overlap; no speed claim. Marker+real library return/exit-entry qualification, not completed process-reap or networking proof.'}
try:
 with (run/'stdout.log').open('wb')as out,(run/'stderr.log').open('wb')as err:
  result=subprocess.run(cmd,stdout=out,stderr=err,timeout=900);report['returncode']=result.returncode
except subprocess.TimeoutExpired:report['timeout']=True
finally:
 report['elapsedSeconds']=time.monotonic()-start
 log=(run/'stdout.log').read_text(errors='replace')
 report['qualified']='reason=hello-qualified' in log and 'failed=0 entries=1 ' in log
 report['passed']=report.get('returncode')==0 and report['qualified']
 (run/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2),flush=True)
