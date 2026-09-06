from pathlib import Path
import os,subprocess,gzip,hashlib,json,datetime
p=Path(__file__).resolve().parent;r=p/'instrumented-run-1';files=['before-picker.snap','post-edit-1.snap','autosave-1.snap.gz','after-reload-1.snap','post-edit-2.snap','autosave-2.snap.gz','after-reload-2.snap'];rows=[]
for name in files:
 f=r/name;h=hashlib.sha256();sourcehash=hashlib.sha256(f.read_bytes()).hexdigest()
 if name.endswith('.gz'):
  fd=os.memfd_create('rtc-read-only-snapshot');n=0
  with gzip.open(f,'rb') as src:
   while data:=src.read(1048576):os.write(fd,data);h.update(data);n+=len(data)
  result=subprocess.run([str(p/'inspect-pending'),f'/proc/{os.getpid()}/fd/{fd}'],check=True,capture_output=True,text=True);os.close(fd)
 else:
  result=subprocess.run([str(p/'inspect-pending'),str(f)],check=True,capture_output=True,text=True);n=f.stat().st_size;h.update(f.read_bytes())
 row=json.loads(result.stdout);row.update(input=name,sourceSHA256=sourcehash,rawSHA256=h.hexdigest(),rawBytes=n,rtcUTC=datetime.datetime.fromtimestamp(row['rtcSeconds'],datetime.timezone.utc).isoformat());assert row['unknownWrites']==0 and row['pending']==[0]*7 and row['guestInstructionsExecuted']==0;rows.append(row)
 assert hashlib.sha256(f.read_bytes()).hexdigest()==sourcehash
 print(name,row['rtcUTC'],flush=True)
report=json.loads((r/'report.json').read_text());assert report['passed'];by={x['input']:x for x in rows}
for n in (1,2):
 a,b=by[f'autosave-{n}.snap.gz'],by[f'after-reload-{n}.snap'];assert b['ticks']>a['ticks'] and b['retired']>a['retired'];assert b['rtcSeconds']>=a['rtcSeconds'];assert abs((b['rtcSeconds']-a['rtcSeconds'])-(b['ticks']-a['ticks'])/a['rtcTickHz'])<1
saved={'passed':True,'scope':'Zero-instruction native snapshot39 reads; gzip expanded into Linux memfd (no extra disk copies), actual RTC/pending/error fields and subsequent restored ticks/retirement. No guest execution or normalized data. All source hashes unchanged.','rows':rows};(r/'export-audit.json').write_text(json.dumps(saved,indent=2)+'\n')
