from pathlib import Path
import subprocess,json,re,hashlib
r=Path.cwd();b=r/'.context/jit-stream-cache';out=[]
ff=str(r/'node_modules/ffmpeg-static/ffmpeg')
for n,slug in [(1,'seed_native_cache_first'),(2,'seed_native_cache_return')]:
 p=json.loads((b/f'mechanism/{n}-persistent/report.json').read_text());source=Path(p['video']);dest=r/f'tweets/videos/{slug}.mp4';thumb=r/f'tweets/thumbnails/{slug}.jpg'
 probe=subprocess.run([ff,'-hide_banner','-threads','1','-i',str(source)],capture_output=True,text=True).stderr
 m=re.search(r'Duration: (\d+):(\d+):(\d+\.\d+)',probe);assert m;duration=int(m[1])*3600+int(m[2])*60+float(m[3])
 cmd=[ff,'-hide_banner','-loglevel','error','-y','-threads','1','-i',str(source),'-an','-c:v','libx264','-threads','1','-preset','fast','-crf','23','-pix_fmt','yuv420p','-movflags','+faststart',str(dest)]
 subprocess.run(cmd,check=True)
 subprocess.run([ff,'-hide_banner','-loglevel','error','-y','-threads','1','-ss',str(max(0,duration-5)),'-i',str(dest),'-frames:v','1','-threads','1','-q:v','2',str(thumb)],check=True)
 outputProbe=subprocess.run([ff,'-hide_banner','-threads','1','-i',str(dest)],capture_output=True,text=True).stderr
 om=re.search(r'Duration: (\d+):(\d+):(\d+\.\d+)',outputProbe);assert om;outputDuration=int(om[1])*3600+int(om[2])*60+float(om[3]);assert abs(outputDuration-duration)<.1
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 out.append(dict(session=n,source=str(source.relative_to(r)),sourceSha256=sha(source),durationSeconds=duration,outputDurationSeconds=outputDuration,mp4=str(dest.relative_to(r)),mp4Sha256=sha(dest),thumbnail=str(thumb.relative_to(r)),thumbnailSha256=sha(thumb),thumbnailAtSeconds=max(0,duration-5),method='Complete uncut source, original speed; H264 one-thread transcode, no fps/rate/time changes',command=cmd))
 (b/'recordings.json').write_text(json.dumps(out,indent=2)+'\n');print(n,duration,outputDuration,flush=True)
