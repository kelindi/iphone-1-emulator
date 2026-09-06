from pathlib import Path
import hashlib,json,re,sys
b=Path('.context/safari-default-mbx')
def audit(data):
 rows=json.loads((b/'rows-audit.json').read_text());by={r['ringOffset']:r for r in rows['results']};records=re.findall(r'(?<![A-Za-z0-9_])STAGED offset(\d+) file(\d+) bytes(\d+) x(\d+) y(\d+) width(\d+) height(\d+)$',(b/'packets-candidate.log').read_text(),re.M)
 assert len(records)==len(by)==905,'actual packet count differs';offset=0
 for fields in records:
  ring,start,n,x,y,w,h=map(int,fields);r=by.pop(ring);assert start==offset and n==r['sourceBytes'] and [x,y,x+w,y+h]==r['destination'],'actual stage bounds differ'
  assert hashlib.sha256(data[start:start+n]).hexdigest()==r['sourceSHA256'],'staged actual pixels differ from independent current source rows';offset+=n
 assert not by and offset==len(data),'staged output coverage differs'
 return {'passed':True,'packets':len(records),'stagedBytes':offset,'sha256':hashlib.sha256(data).hexdigest(),'pixelsCommittedToGuest':False,'guestExecuted':False,'oracle':'Independent original packet fields and actual per-row GART source reads'}
if __name__=='__main__':
 data=Path(sys.argv[1]).read_bytes();report=audit(data);print(json.dumps(report))
 if len(sys.argv)>2:Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
