from pathlib import Path
import json,struct,zlib,hashlib,collections
root=Path('.context/snapshot-page-scan')
def png(path):
 b=Path(path).read_bytes();assert b[:8]==b'\x89PNG\r\n\x1a\n';at=8;payload=b''
 while at<len(b):
  n=struct.unpack('>I',b[at:at+4])[0];tag=b[at+4:at+8];data=b[at+8:at+8+n];at+=12+n
  if tag==b'IHDR':w,h,bits,kind,compression,filtering,interlace=struct.unpack('>IIBBBBB',data);assert bits==8 and kind in (2,6) and interlace==0
  if tag==b'IDAT':payload+=data
 raw=zlib.decompress(payload);channels=4 if kind==6 else 3;stride=w*channels;last=bytearray(stride);out=bytearray();at=0
 for y in range(h):
  filter=raw[at];row=bytearray(raw[at+1:at+1+stride]);at+=stride+1
  for x in range(stride):
   a=row[x-channels] if x>=channels else 0;b=last[x];c=last[x-channels] if x>=channels else 0
   if filter==1:v=a
   elif filter==2:v=b
   elif filter==3:v=(a+b)//2
   elif filter==4:
    p=a+b-c;pa,pb,pc=abs(p-a),abs(p-b),abs(p-c);v=a if pa<=pb and pa<=pc else b if pb<=pc else c
   else:assert filter==0;v=0
   row[x]=(row[x]+v)&255
  out+=row if channels==4 else b"".join(bytes(row[i:i+3])+bytes([255]) for i in range(0,len(row),3));last=row
 return w,h,bytes(out)

paths=sorted(root.glob('browser-*/*-save.png'));assert len(paths)==8
frames=[png(p) for p in paths];w,h,ref=frames[0];results=[]
for p,(width,height,actual) in zip(paths,frames):
 assert (width,height)==(w,h)
 maximum=0;bad=0;pixels=0
 for y in range((30*h+479)//480,h):
  for x in range(w):
   at=(y*w+x)*4;delta=max(abs(actual[at+c]-ref[at+c]) for c in range(3));maximum=max(maximum,delta);bad+=delta>3;pixels+=1
 results.append({'file':str(p),'reference':str(paths[0]),'maxRGBDelta':maximum,'pixelsOver3':bad,'comparedPixels':pixels,'passed':bad==0})
report={'method':'Independent PNG decoding; everyRGB pixel compared to first baseline Home below predefined scaled30-pixel status-bar exclusion; unchanged allowedRGBdelta3.','width':w,'height':h,'results':results,'passed':all(r['passed'] for r in results)}
(root/'screen-review.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
# Contact sheet from original decoded buffers, no rescaling or omitted samples.
cols=4;rows=2
with (root/'screen-contact.ppm').open('wb') as out:
 out.write(f'P6\n{w*cols} {h*rows}\n255\n'.encode())
 for row in range(rows):
  for y in range(h):
   for col in range(cols):
    raw=frames[row*cols+col][2]
    out.write(b''.join(raw[(y*w+x)*4:(y*w+x)*4+3] for x in range(w)))
