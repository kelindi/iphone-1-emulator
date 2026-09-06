from pathlib import Path
import json,struct,zlib,hashlib,collections
root=Path(__file__).resolve().parent
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

results=[]; frames=[]
for name in ['04-celsius','08-city-removed','10-fahrenheit']:
 a=root/(name+'-saved-guest.png'); b=root/(name+'-restored-guest.png')
 w,h,left=png(a); w2,h2,right=png(b); assert (w,h)==(w2,h2)
 start=(30*h+479)//480*w*4
 assert left[start:]==right[start:]
 results.append({'stage':name,'allRGBAExact':True,'comparedPixels':(len(left)-start)//4})
 frames += [(w,h,left),(w,h,right)]
# Actual wrong selections/retained row must fail this same complete-frame gate.
negatives=[]
for expected,wrong in [('04-celsius-restored','02-cities-f'),('08-city-removed-restored','04-celsius-restored'),('10-fahrenheit-restored','08-city-removed-restored')]:
 w,h,a=png(root/(expected+'-guest.png')); w2,h2,b=png(root/(wrong+'-guest.png')); assert (w,h)==(w2,h2)
 start=(30*h+479)//480*w*4
 changed=sum(a[i:i+4]!=b[i:i+4] for i in range(start,len(a),4));assert changed>0
 negatives.append({'expected':expected,'wrongState':wrong,'differentPixels':changed,'rejected':True})
report={'passed':True,'method':'Independent PNG decoding and exact RGBA equality below scaled original30px status exclusion; actual wrong units/city state negatives. Separate original canvas gate compares144000RGB pixels perreload.','pairs':results,'wrongStateNegatives':negatives}
(root/'screen-review.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
with (root/'screen-contact.ppm').open('wb') as f:
 f.write(('P6\n%d %d\n255\n'%(w*2,h*3)).encode())
 for row in range(3):
  for y in range(h):
   for col in range(2):
    raw=frames[row*2+col][2];f.write(b''.join(raw[(y*w+x)*4:(y*w+x)*4+3] for x in range(w)))
