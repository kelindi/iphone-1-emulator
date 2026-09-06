from pathlib import Path
import subprocess,json,re,hashlib
b=Path('.context/jit-sw-cache-v4');d=Path('docs/traces/jit-stream-cache/sw-cache-offline');ffmpeg=Path('node_modules/ffmpeg-static/ffmpeg').resolve();rows=[]
for phase,slug,thumb in [('1-train','seed_cache_offline_training',40),('2-return','seed_cache_offline_return',12)]:
 source=next((b/'run'/phase/'video').glob('*.webm'));out=Path('tweets/videos')/(slug+'.mp4');jpg=Path('tweets/thumbnails')/(slug+'.jpg')
 command=[str(ffmpeg),'-y','-threads','1','-i',str(source),'-c:v','libx264','-threads','1','-preset','fast','-crf','23','-pix_fmt','yuv420p','-an','-movflags','+faststart',str(out)]
 with (d/(slug+'-encode.log')).open('w') as f:subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True)
 with (d/(slug+'-thumbnail.log')).open('w') as f:subprocess.run([str(ffmpeg),'-y','-threads','1','-ss',str(thumb),'-i',str(out),'-frames:v','1','-threads','1',str(jpg)],stdout=f,stderr=subprocess.STDOUT,check=True)
 def duration(p):
  result=subprocess.run([str(ffmpeg),'-i',str(p)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE);m=re.search(r'Duration: (\d+):(\d+):(\d+\.\d+)',result.stderr);assert m;return int(m[1])*3600+int(m[2])*60+float(m[3])
 a,c=duration(source),duration(out);assert a==c,(a,c)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();rows.append({'phase':phase,'source':str(source),'sourceSha256':sha(source),'video':str(out),'videoSha256':sha(out),'thumbnail':str(jpg),'thumbnailSha256':sha(jpg),'thumbnailSeconds':thumb,'sourceDurationSeconds':a,'outputDurationSeconds':c,'uncut':True,'speedChanged':False,'command':command})
(d/'recordings.json').write_text(json.dumps(rows,indent=2)+'\n');print(rows)
