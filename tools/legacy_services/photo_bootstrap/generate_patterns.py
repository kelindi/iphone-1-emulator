"""Generate the five own deterministic JPEG test patterns with the pinned encoder."""
from pathlib import Path
import argparse, hashlib, subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--encoder',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();encoder=args.encoder.resolve();output=args.output
assert hashlib.sha256(encoder.read_bytes()).hexdigest()=='ed652b2f32e0851d1946894fb8333f5b677c1b2ce6b9d187910a67f8b99da028', 'Unqualified encoder'
output.mkdir(parents=True,exist_ok=False)
def render(num,bg,circle,width,height,dest):
 raw=bytearray()
 for y in range(height):
  for x in range(width):
   xx=x*640/width; yy=y*480/height; color=bg
   inside=(xx-320)**2+(yy-250)**2<=160**2 if circle else 160<=xx<=480 and 90<=yy<=410
   if inside:color=(250,222,75) if circle else (90,230,180)
   if any(40+i*65<=xx<=80+i*65 and 25<=yy<=65 for i in range(num)):color=(255,255,255)
   raw.extend(color)
 ppm=('P6\n%d %d\n255\n'%(width,height)).encode()+raw
 subprocess.run([str(encoder),'-hide_banner','-loglevel','error','-f','image2pipe','-vcodec','ppm','-i','pipe:0','-frames:v','1','-threads','1','-c:v','mjpeg','-q:v','2','-f','image2',str(dest)],input=ppm,check=True)
for num,bg,circle in [(1,(30,92,164),True),(2,(201,69,42),False),(3,(90,90,90),False)]:
 name='IMG_%04d.JPG'%num if num<3 else 'BAD.JPG'
 render(num,bg,circle,640,480,output/name)
 if num<3:render(num,bg,circle,64,48,output/('IMG_%04d.THM'%num))
