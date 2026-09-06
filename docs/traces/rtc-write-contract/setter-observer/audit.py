from pathlib import Path
import re,json,struct,datetime,hashlib,shutil
p=Path('.context/rtc-contract/setter-observer');out=p/'run-1';text=(out/'trace.log').read_text();run=json.loads((out/'run.json').read_text())
assert run['exitCode']==10 and not run['timedOut']
expected=['settimeofday','clock_set_calendar','platform_set_gmt','arm_platform_set_gmt','rtc_resource_return','rtc_platform_dispatch','rtc_set_gmt','rtc_set_date','queue_date']
observed=re.findall(r'^OBSERVED pre-step (\w+) cycles(\d+) ticks(\d+) pc([0-9a-f]+) cpsr([0-9a-f]+) lr([0-9a-f]+) r0([0-9a-f]+) r1([0-9a-f]+) r2([0-9a-f]+) r3([0-9a-f]+)$',text,re.M)
retired=re.findall(r'^ORDER retired source([0-9a-f]+) expected([0-9a-f]+) actual([0-9a-f]+) mode([0-9a-f]+) cycles(\d+)$',text,re.M)
assert len(observed)==len(retired)==18 and [r[0] for r in observed]==expected*2
for a,b in zip(observed,retired):assert a[3]==b[0] and b[1]==b[2] and int(a[1])+1==int(b[4]) and int(a[4],16)&0x3f==int(b[3],16)
assert 'ORDER unretired' not in text and 'RTC write ordinal' not in text
assert 'END retired400000000 records18 rtcWrites0 callbackExit0 observationFailed0' in text
values=re.findall(r'^PEEK timeval address[0-9a-f]+ bytes([0-9a-f]+)$',text,re.M);dates=re.findall(r'^PEEK date-structure address[0-9a-f]+ bytes([0-9a-f]+)$',text,re.M)
assert len(values)==2 and len(dates)==4
entries=[]
for i,value in enumerate(values):
 seconds,micros=struct.unpack('<II',bytes.fromhex(value));assert micros==0
 dt=datetime.datetime.fromtimestamp(seconds+1,datetime.timezone.utc)
 desired=bytes([dt.second,dt.minute,dt.hour,(dt.weekday()+1)%7,dt.day,dt.month])+struct.pack('<H',dt.year)
 assert bytes.fromhex(dates[i*2])[:8]==bytes.fromhex(dates[i*2+1])[:8]==desired
 entries.append({'requestedUnixSeconds':seconds,'microseconds':micros,'originalDriverPlusOneSecond':dt.isoformat(),'dateSemanticBytes':desired.hex(),'structurePaddingNotInterpreted':True})
r={'passedObservationAudit':True,'passedRTCWriteGate':False,'scope':'Two observed original setter/queue-action entries retired; original date conversion agrees. No asynchronous callback or hardwareRTCwrite within fixed400M cap. Entering a queue action does not prove its whole body or signal completed.','retiredBoundaries':18,'requests':entries,'traceSHA256':hashlib.sha256(text.encode()).hexdigest(),'endPC':'c01411b8'}
(out/'audit.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
d=Path('docs/traces/rtc-write-contract/setter-observer');d.mkdir(exist_ok=True)
for f in ['probe.c','audit.py','build.log','pins.json']:shutil.copy2(p/f,d/f)
for f in ['trace.log','run.json','audit.json']:shutil.copy2(out/f,d/f)
