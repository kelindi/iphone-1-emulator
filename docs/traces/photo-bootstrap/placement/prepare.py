"""Disposable DCF hypothesis. Never modify a retained source or mounted guest."""
from pathlib import Path
import sys,os,json,hashlib,tempfile,struct,importlib.util,subprocess
B=Path(__file__).resolve().parent
sys.path.insert(0,str(Path('tools/firmware').resolve()))
sys.path.insert(0,str(Path('.context/guest-safari').resolve()))
import rootfs_work as W,hfsx_extract as H
spec=importlib.util.spec_from_file_location('old_installer',Path('.context/guest-safari/prepare_image.py'));old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
SOURCE=Path('.context/graphics-speed/rootfs-mbx2d.img');SOURCE_SHA='73d0d2a0ed4564dc16c67c7c6efc167725d2833461cd47919faa676595c0ded9'
sha=lambda b:hashlib.sha256(b).hexdigest()
TARGET=B/'photos.img';assert not TARGET.exists()
assets=B/'assets';assets.mkdir(exist_ok=False)
encoder=Path('node_modules/ffmpeg-static/ffmpeg').resolve()
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
 render(num,bg,circle,640,480,assets/name)
 if num<3:render(num,bg,circle,64,48,assets/('IMG_%04d.THM'%num))
adds=[('/private/var/root/Media/DCIM/100APPLE/'+p.name,p)for p in sorted(assets.iterdir())]
report={'scope':'Own test images; candidate Photos process home path, DCF image/THM hypothesis, invalid-name control. No guest/Camera/default claim.','source':str(SOURCE),'sourceSHA256':SOURCE_SHA,'encoderSHA256':sha(encoder.read_bytes()),'newFiles':[],'catalogAdmission':[],'passed':False}
class CheckedWork(W.Work):
 def _insert_catalog_record(self,record):
  key=self._key(record);selected=None
  for idx,node in self._leaf_chain():
   rows=[r[2]for r in self._leaf_records(node)];selected=(idx,rows)
   if not rows or self._key(rows[-1])>=key:break
  assert selected
  idx,rows=selected;assert rows and key>self._key(rows[0]),'Existing index first key would change'
  assert all(self._key(r)!=key for r in rows)
  used=14+sum(map(len,rows))+len(record)+2*(len(rows)+2);assert used<=self.node_size,'No catalog split allowed'
  report['catalogAdmission'].append({'node':idx,'used':used,'capacity':self.node_size,'firstKeyUnchanged':True})
  super()._insert_catalog_record(record)
raw=SOURCE.read_bytes();assert sha(raw)==SOURCE_SHA
before,forks=old.inventory(SOURCE);assert not any('/Media/DCIM/'in p for p in before)
fd,name=tempfile.mkstemp(prefix='photos-',suffix='.partial',dir=B);tmp=Path(name)
try:
 with os.fdopen(fd,'wb')as f:f.write(raw)
 w=CheckedWork(tmp)
 for path,asset in adds:w.add_file(path,asset,mode=0o644,uid=0,gid=0)
 w.vol.f.close();after,newforks=old.inventory(tmp)
 newdirs={'/private/var/root/Media','/private/var/root/Media/DCIM','/private/var/root/Media/DCIM/100APPLE'}
 assert set(after)-set(before)==newdirs|{a[0]for a in adds}
 for path,data in before.items():
  now=after[path]
  if path=='/private/var/root':assert H.u32(now,4)==H.u32(data,4)+1 and now[:4]+now[8:]==data[:4]+data[8:]
  else:assert now==data,path
 for path,oldfork in forks.items():assert newforks[path]==oldfork,path
 for path,asset in adds:
  content=asset.read_bytes();assert newforks[path][0]=={'bytes':len(content),'sha256':sha(content)}
  assert H.u16(after[path],42)==0o100644 and H.u32(after[path],32)==H.u32(after[path],36)==0
  report['newFiles'].append({'path':path,'bytes':len(content),'sha256':sha(content)})
 assert sha(SOURCE.read_bytes())==SOURCE_SHA
 report.update(passed=True,unchangedOldForks=len(forks)*2,newDirectories=sorted(newdirs),imageSHA256=sha(tmp.read_bytes()))
 os.link(tmp,TARGET)
finally:
 tmp.unlink(missing_ok=True)
 (B/'preparation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
