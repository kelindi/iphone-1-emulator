from pathlib import Path
import re,json,datetime,hashlib,sys
out=Path(sys.argv[1]) if len(sys.argv)>1 else Path('.context/rtc-write/run-1')
text=(out/'trace.log').read_text();run=json.loads((out/'run.json').read_text())
assert run['exitCode']==0 and not run['timedOut']
prior=Path('.context/rtc-second/run-1/trace.log').read_text()
assert text.split('PENDING ')[0]==prior.split('END retired')[0], 'original079 instruction/write prefix changed'
pending=re.findall(r'^PENDING seconds(\d+) fraction(\d+) tickHz(\d+) ticks(\d+) registers([0-9a-f]{14}) mask1111111$',text,re.M)
commit=re.findall(r'^COMMIT seconds(\d+) fraction(\d+) tickHz(\d+) ticks(\d+) cycles(\d+) pc([0-9a-f]+) unknownWrites0$',text,re.M)
assert len(pending)==len(commit)==1
old,phase,hz,ticks,raw=pending[0];old,phase,hz,ticks=map(int,[old,phase,hz,ticks])
new,newphase,newhz,newticks,cycles,pc=commit[0];new,newphase,newhz,newticks,cycles=map(int,[new,newphase,newhz,newticks,cycles])
assert hz==newhz and 0<=phase<hz and newticks>=ticks and cycles>=12523518323
deadline=ticks+(hz-phase);assert newticks>=deadline
values=bytes.fromhex(raw)
writes=re.findall(r'^RTC write ordinal(\d+) register([0-9a-f]+) value([0-9a-f]+) accepted1 ',text,re.M)
assert [(int(i),int(r,16),int(v,16)) for i,r,v in writes]==[(i+1,0x59+i,v) for i,v in enumerate(values)]
def unbcd(n):
    assert (n&15)<10 and n>>4<10
    return (n>>4)*10+(n&15)
s,m,h=map(unbcd,values[:3]);d,mo,y=map(unbcd,values[4:])
dt=datetime.datetime(2000+y,mo,d,h,m,s,tzinfo=datetime.timezone.utc)
assert (dt.weekday()+1)%7==values[3]
expected=int(dt.timestamp())+(newticks-deadline)//hz
assert new==expected and newphase==(newticks-deadline)%hz, 'commit did not match actual bytes/elapsed pulse phase'
assert old!=new, 'this fixture must expose an actual clock change'
end=re.search(r'END retired(\d+) records(\d+) rtcWrites7 callbackExit1 observationFailed0 pc([0-9a-f]+) unknownWrites0',text);assert end
assert int(end[1])<=400000000 and end[3]==pc and int(end[1])==cycles-12498269215
result={'passed':True,'scope':'Actual original079 prefix unchanged; complete original driver RTC update becomes active at the next real pulse. No browser/persistence/production acceptance claimed.','requestedDate':dt.isoformat(),'oldActiveSeconds':old,'newActiveSeconds':new,'pendingAtTicks':ticks,'nextPulseTicks':deadline,'actualCommitTicks':newticks,'fraction':newphase,'retiredInstructions':int(end[1]),'traceSHA256':hashlib.sha256(text.encode()).hexdigest()}
(out/'audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
