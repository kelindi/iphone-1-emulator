from pathlib import Path
import subprocess,signal,os,json,time,shutil,hashlib
p=Path('.context/web-lto').resolve();out=p/'timing';assert not out.exists(),'No duplicate timing series';pins=json.loads((p/'timing-pins.json').read_text())
for f,h in pins['files'].items():assert hashlib.sha256(Path(f).read_bytes()).hexdigest()==h,f
assert shutil.disk_usage('.').free>4.5*2**30,'headroom for at most one failure snapshot'
out.mkdir();report={'passed':False,'scope':'One prewarmed600M ABBA series, no retry;420s total process cap','startedUTC':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'freeBefore':shutil.disk_usage('.').free}
with open(out/'run.log','w')as log:
 child=subprocess.Popen(['node',str(p/'timing.mjs')],stdout=log,stderr=subprocess.STDOUT,start_new_session=True);report['pid']=child.pid
 try:report['exitCode']=child.wait(timeout=420)
 except subprocess.TimeoutExpired:report['timeout']=True;os.killpg(child.pid,signal.SIGKILL);report['exitCode']=child.wait()
report['endedUTC']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());report['processClosed']=not Path('/proc',str(child.pid)).exists();report['freeAfter']=shutil.disk_usage('.').free;report['passed']=report['exitCode']==0 and report['processClosed']
(out/'controller.json').write_text(json.dumps(report,indent=2));print(json.dumps(report));raise SystemExit(0 if report['passed']else 1)
