from pathlib import Path
import json,struct,zlib,hashlib,collections
root=Path(__file__).resolve().parent/'calculator-recorded'
def png(path):
 b=Path(path).read_bytes();assert b[:8]==b'\x89PNG\r\n\x1a\n';at=8;payload=b''
 while at<len(b):
  n=struct.unpack('>I',b[at:at+4])[0];tag=b[at+4:at+8];data=b[at+8:at+8+n];at+=12+n
  if tag==b'IHDR':w,h,bits,kind,compression,filtering,interlace=struct.unpack('>IIBBBBB',data);assert bits==8 and kind==6 and interlace==0
  if tag==b'IDAT':payload+=data
 raw=zlib.decompress(payload);stride=w*4;last=bytearray(stride);out=bytearray();at=0
 for y in range(h):
  filter=raw[at];row=bytearray(raw[at+1:at+1+stride]);at+=stride+1
  for x in range(stride):
   a=row[x-4] if x>=4 else 0;b=last[x];c=last[x-4] if x>=4 else 0
   if filter==1:v=a
   elif filter==2:v=b
   elif filter==3:v=(a+b)//2
   elif filter==4:
    p=a+b-c;pa,pb,pc=abs(p-a),abs(p-b),abs(p-c);v=a if pa<=pb and pa<=pc else b if pb<=pc else c
   else:assert filter==0;v=0
   row[x]=(row[x]+v)&255
  out+=row;last=row
 return w,h,bytes(out)
refs=json.loads(Path('web/tests/include/calculator-displays.json').read_text());report=json.loads((root/'report.json').read_text());result={'method':'Independent stdlib PNG decoder; each original RGBA buffer is SHA256-verified against the maintained reference. Original display pixels retained. Background is original reference column0 at each row; foreground is its most frequent non-background RGB.','cases':[]}
for i,c in enumerate(report['cases'],1):
 name=f'{i:02d}-'+c['name'];reference=refs['displays'][c['expected']];w,h,a=png(root/(name+'-display.png'));rw,rh,b=png(Path('web/tests/include')/reference['image']);assert (w,h)==(rw,rh)==(320,88);assert hashlib.sha256(b).hexdigest()==reference['sha256']
 def rgb(buf,x,y):return tuple(buf[(y*w+x)*4:(y*w+x)*4+3])
 bg=[rgb(b,0,y) for y in range(h)];colors=collections.Counter(rgb(b,x,y) for y in range(h) for x in range(w) if rgb(b,x,y)!=bg[y]);fg=colors.most_common(1)[0][0]
 hist=collections.Counter();channels=collections.Counter();changed=0;two=[];positions=[];alpha=True
 for y in range(h):
  for x in range(w):
   aa,bb=rgb(a,x,y),rgb(b,x,y);ds=[abs(aa[k]-bb[k]) for k in range(3)];d=max(ds);hist[d]+=1
   for v in ds:channels[v]+=1
   alpha &= a[(y*w+x)*4+3]==b[(y*w+x)*4+3]
   if d:changed+=1;positions.append((x,y))
   if d==2:two.append({'x':x,'y':y,'original':bb,'actual':aa,'background':bg[y],'originalIsIntermediateGlyphColor':bb!=bg[y] and bb!=fg})
 result['cases'].append({'name':name,'expected':c['expected'],'manuallyRead':c['expected'],'maxRGBDelta':max(hist),'pixelDeltaHistogram':dict(sorted(hist.items())),'channelDeltaHistogram':dict(sorted(channels.items())),'changedPixels':changed,'alphaExact':alpha,'dominantGlyphRGB':fg,'twoLevelPixels':two,'twoLevelConfinedToIntermediateGlyphColors':all(p['originalIsIntermediateGlyphColor'] for p in two),'changedBoundingBox':[min(x for x,y in positions),min(y for x,y in positions),max(x for x,y in positions),max(y for x,y in positions)] if positions else None})
(Path(__file__).resolve().parent/'calculator-pixel-review.json').write_text(json.dumps(result,indent=2)+'\n')
for c in result['cases']:print(c['name'],c['expected'],'pixels',c['pixelDeltaHistogram'],'channels',c['channelDeltaHistogram'],'twoAA',c['twoLevelConfinedToIntermediateGlyphColors'])
