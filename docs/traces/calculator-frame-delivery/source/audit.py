"""Independent retained-raw-crop audit. Frozen references are ordinary RGBA PNGs."""
from pathlib import Path
import hashlib,json,gzip,zlib,struct,math,statistics,sys,binascii
sha=lambda b:hashlib.sha256(b).hexdigest()
def png_rgba(path):
 b=Path(path).read_bytes();assert b[:8]==b'\x89PNG\r\n\x1a\n';p=8;data=b'';header=None
 while p<len(b):
  n=struct.unpack('>I',b[p:p+4])[0];kind=b[p+4:p+8];body=b[p+8:p+8+n];assert len(body)==n;assert binascii.crc32(kind+body)&0xffffffff==struct.unpack('>I',b[p+8+n:p+12+n])[0];p+=12+n
  if kind==b'IHDR':header=struct.unpack('>IIBBBBB',body)
  if kind==b'IDAT':data+=body
  if kind==b'IEND':break
 assert header==(320,88,8,6,0,0,0) and p==len(b)
 raw=zlib.decompress(data);stride=1280;assert len(raw)==88*(stride+1);out=bytearray();previous=bytearray(stride)
 def paeth(a,b,c):
  q=a+b-c;pa,pb,pc=abs(q-a),abs(q-b),abs(q-c);return a if pa<=pb and pa<=pc else b if pb<=pc else c
 for y in range(88):
  f=raw[y*(stride+1)];row=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)]);assert f<=4
  for x in range(stride):
   a=row[x-4]if x>=4 else 0;b=previous[x];c=previous[x-4]if x>=4 else 0
   value=[0,a,b,(a+b)//2,paeth(a,b,c)][f];row[x]=(row[x]+value)&255
  out.extend(row);previous=row
 return bytes(out)
def compare(a,b):
 assert len(a)==len(b);delta=max(abs(a[i]-b[i])for i in range(len(a))if i%4!=3);alpha=all(a[i]==b[i]for i in range(3,len(a),4));return {'passed':delta<=2 and alpha,'maxRGBDelta':delta,'alphaExact':alpha}
def button(a,b):
 assert len(a)==len(b)==6400;return sum(max(abs(a[i+k]-b[i+k])for k in range(3))>12 for i in range(0,len(a),4))
def references():
 r=json.loads(Path('web/tests/include/calculator-displays.json').read_text());out={}
 for value in ['0','9']:
  ref=r['displays'][value];raw=png_rgba(Path('web/tests/include')/ref['image']);assert sha(raw)==ref['sha256'];out[value]=raw
 return out
def check_row(r,root,refs):
 def crop(packet,n):
  file=packet['file'];assert file.startswith('crops/')and'..'not in file;raw=gzip.decompress((root/file).read_bytes());assert len(raw)==packet['byteLength']==n and sha(raw)==packet['sha256'];return raw
 assert not r.get('failed')and not r['timedOut']and not r['errors'];assert r['down']<r['up'];base=crop(r['baseline']['bytes'],6400)
 actual={}
 for name,n in [('firstButton',6400),('firstResult',112640),('rafFirstButton',6400),('rafFirstResult',112640)]:
  event=r[name];raw=crop(event['crop']['bytes'],n);actual[name]=raw;assert event['sequence']==event['crop']['frame']
  if name.startswith('first'):assert raw==crop(event['crop']['sourceBytes'],n)
  if 'Button'in name:assert button(raw,base)>=16 and button(raw,base)==event['changed']
  else:assert compare(raw,refs[r['value']])==event['comparison']and event['comparison']['passed']
 confirm=crop(r['confirmed']['bytes'],112640);assert confirm==crop(r['confirmed']['sourceBytes'],112640);assert compare(confirm,refs[r['value']])==r['confirmation']and r['confirmation']['passed'];assert r['confirmedAt']>=r['rafFirstResult']['at']+100
 frames={x['sequence']:x for x in r['frames']};assert len(frames)==len(r['frames'])
 for name in ['firstButton','firstResult']:
  e=r[name];f=frames[e['sequence']];assert e['receivedAt']==f['receivedAt']and e['canvasDoneAt']==f['canvasDoneAt'];assert f['receivedAt']<=f['sourceCopiedAt']<=f['beforeNativeAt']<=f['canvasDoneAt']<=f['observerDoneAt'];assert f['sourceCanvasExact']
 for name in ['rafFirstButton','rafFirstResult']:
  e=r[name];assert e['sequence']in frames and e['canvasDoneAt']==frames[e['sequence']]['canvasDoneAt']and e['at']>=e['canvasDoneAt'];assert any(x['sequence']==e['sequence']and x['at']==e['at']for x in r['raf'])
 assert r['confirmed']['frame']==r['frames'][-1]['sequence']and r['confirmed']['at']==r['confirmedAt']
 for e in r['raf']:assert e['sequence']in frames and e['canvasDoneAt']==frames[e['sequence']]['canvasDoneAt'] and e['at']>=e['canvasDoneAt']
 return {'key':r['key'],'round':r['round'],'actualButtonFrame':r['firstButton']['sequence'],'rafButtonFrame':r['rafFirstButton']['sequence'],'canvasButtonMs':r['firstButton']['canvasDoneAt']-r['down'],'rafButtonMs':r['rafFirstButton']['at']-r['down'],'canvasResultMs':r['firstResult']['canvasDoneAt']-r['down']}
if __name__=='__main__':
 if len(sys.argv)==1:
  refs=references();assert not compare(refs['0'],refs['9'])['passed'];print('Both frozen PNG pixel hashes and wrong-reference negative pass');sys.exit(0)
 root=Path(sys.argv[1]);report=json.loads((root/'report.json').read_text());assert report['errors']==[]and report['correctnessPassed']and report['observerErrors']==[];assert len(report['samples'])==60
 rows=[check_row(x,root,references())for x in report['warmup']+report['samples']]
 for key in ['9','C']:
  a=[r for r in rows if r['key']==key and r['round']>0];assert len(a)==30
  for field,metric in [('canvasButtonMs','canvasButton'),('rafButtonMs','rafButton'),('canvasResultMs','canvasResult')]:
   values=sorted(x[field]for x in a);summary=report['summary'][key][metric];assert abs(summary['p95Ms']-values[28])<1e-6;assert abs(summary['medianMs']-statistics.median(values))<1e-6
 out={'passed':True,'measuredRawRows':60,'warmupRawRows':2,'rows':rows,'scope':'Recomputed retained actual/source/RAF button and display crops against hashed independent originalPNG references. Baseline is actual prepress canvas with latest frame sequence, no independently retained baseline sourceROI. Earliest detection relies on complete per-frame metadata; raw crops retained at selected evidence points.'};(root/'independent-pixel-audit.json').write_text(json.dumps(out,indent=2));print('Raw pixel evidence and percentile recomputation PASS')
