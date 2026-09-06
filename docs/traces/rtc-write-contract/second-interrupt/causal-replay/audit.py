from pathlib import Path
import re,json,struct,datetime,hashlib,sys
base=Path('.context/rtc-second');out=Path(sys.argv[1]) if len(sys.argv)>1 else base/'run-1'
text=(out/'trace.log').read_text();run=json.loads((out/'run.json').read_text())
assert run['exitCode']==0 and not run['timedOut']
assert 'observationFailed0' in text
observations=[];pending=None;writes=[]
for line in text.splitlines():
    if line.startswith('OBSERVED pre-step '):
        assert pending is None
        m=re.fullmatch(r'OBSERVED pre-step (\w+) cycles(\d+) ticks(\d+) pc([0-9a-f]+) cpsr([0-9a-f]+) lr([0-9a-f]+) r0([0-9a-f]+) r1([0-9a-f]+) r2([0-9a-f]+) r3([0-9a-f]+)',line);assert m
        name,cycles,ticks,pc,cpsr,lr,r0,r1,r2,r3=m.groups()
        pending=dict(name=name,cycles=int(cycles),pc=int(pc,16),mode=int(cpsr,16)&63,peeks={})
    elif line.startswith('PEEK '):
        m=re.fullmatch(r'PEEK ([\w-]+) address[0-9a-f]+ bytes([0-9a-f]+)',line);assert m and pending
        pending['peeks'][m[1]]=bytes.fromhex(m[2])
    elif line.startswith('ORDER '):
        m=re.fullmatch(r'ORDER (retired|unretired) source([0-9a-f]+) expected([0-9a-f]+) actual([0-9a-f]+) mode([0-9a-f]+) cycles(\d+)',line);assert m and pending
        assert int(m[2],16)==pending['pc'] and int(m[6])==pending['cycles']+1
        if m[1]=='retired':
            assert m[3]==m[4]
            assert int(m[5],16)&31==19
            if pending['name']!='pmu_write_call':assert int(m[5],16)==pending['mode']
            observations.append(pending)
        pending=None
    elif line.startswith('RTC write '):
        m=re.fullmatch(r'RTC write ordinal(\d+) register([0-9a-f]+) value([0-9a-f]+) accepted([01]) cycles(\d+) ticks(\d+) pc([0-9a-f]+) cpsr([0-9a-f]+)',line);assert m
        writes.append((int(m[1]),int(m[2],16),int(m[3],16),int(m[4])))
assert pending is None
path=['settimeofday','clock_set_calendar','platform_set_gmt','arm_platform_set_gmt','rtc_resource_return','rtc_platform_dispatch','rtc_set_gmt','rtc_set_date','queue_date']
names=[r['name'] for r in observations];assert names[:9]==path
requests=[];current=None;queued=None;callback=None;transaction=None
for rec in observations:
    name=rec['name'];peeks=rec['peeks']
    if name=='settimeofday':
        seconds,micros=struct.unpack('<II',peeks['timeval']);assert micros==0
        dt=datetime.datetime.fromtimestamp(seconds+1,datetime.timezone.utc)
        current=bytes([dt.second,dt.minute,dt.hour,(dt.weekday()+1)%7,dt.day,dt.month])+struct.pack('<H',dt.year)
        requests.append({'unixSeconds':seconds,'date':dt.isoformat(),'semanticBytes':current.hex()})
    elif name in ['rtc_set_date','queue_date']:
        assert current and peeks['date-structure'][:8]==current
        if name=='queue_date':queued=current
    elif name=='async_callback':
        assert callback is None and queued
        data=peeks['pending-date-and-flag'];assert data[:8]==queued and data[8]==1
        callback=queued
    elif name=='pmu_write_call':
        assert callback and transaction is None
        def bcd(n):return (n//10)*16+n%10
        d=callback;desired=bytes([bcd(d[0]),bcd(d[1]),bcd(d[2]),d[3],bcd(d[4]),bcd(d[5]),bcd(struct.unpack('<H',d[6:8])[0]%100)])
        transaction=peeks['actual-pmu-write'];assert transaction==desired
assert names[-4:]==['async_callback','pmu_write_call','pmu_write_return','callback_exit']
assert transaction and writes==[(i+1,0x59+i,v,1) for i,v in enumerate(transaction)]
end=re.search(r'END retired(\d+) records(\d+) rtcWrites7 callbackExit1 observationFailed0 pc([0-9a-f]+) unknownWrites7',text);assert end
assert int(end[1])<=400000000 and end[3]=='c03ffb2c'
result={'passed':True,'scope':'Original queued date callback and seven delegated I2C bytes after isolated SECOND latch; RTC writes remain unsupported. No UI/persistence/production claim.','retiredBoundaries':len(observations),'requests':requests,'callbackSemanticDate':callback.hex(),'actualRegister59Bytes':transaction.hex(),'retiredInstructions':int(end[1]),'traceSHA256':hashlib.sha256(text.encode()).hexdigest()}
(out/'audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
