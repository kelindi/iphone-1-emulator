from pathlib import Path
import subprocess,json,time,resource
b=Path('.context/runtime-comparison');ff=str(Path('node_modules/ffmpeg-static/ffmpeg').resolve());video=json.loads((b/'browser-b1/report.json').read_text())['video'];raw=b/'phone-motion.rgb'
# Screen at x480,y247 follows the exact380x716 CSS frame at this1280x1000 viewport.
with (b/'decode-source.log').open('x')as log:subprocess.run([ff,'-nostdin','-v','error','-threads','1','-i',video,'-t','20','-vf','crop=320:480:480:247,fps=30','-pix_fmt','rgb24','-f','rawvideo',str(raw)],stdout=log,stderr=log,check=True)
frames=raw.stat().st_size//(320*480*3);rows=[]
for i in range(3):
 out=b/f'encoded-{i}.h264';cmd=[ff,'-nostdin','-v','error','-threads','1','-f','rawvideo','-pixel_format','rgb24','-video_size','320x480','-framerate','30','-i',str(raw),'-c:v','libx264','-preset','ultrafast','-tune','zerolatency','-crf','23','-g','30','-bf','0','-threads','1','-pix_fmt','yuv420p','-f','h264',str(out)]
 usage=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter()
 with (b/f'encoder-{i}.log').open('x')as log:subprocess.run(cmd,stdout=log,stderr=log,check=True)
 elapsed=time.perf_counter()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN);cpu=after.ru_utime+after.ru_stime-usage.ru_utime-usage.ru_stime
 rows.append({'trial':i,'frames':frames,'seconds':elapsed,'cpuSeconds':cpu,'cpuMsPerFrame':cpu*1000/frames,'bytes':out.stat().st_size,'megabitsPerSecond':out.stat().st_size*8/(frames/30)/1e6})
with (b/'encoded-full-decode.log').open('x')as log:subprocess.run([ff,'-nostdin','-v','error','-threads','1','-i',str(b/'encoded-0.h264'),'-f','null','-'],stdout=log,stderr=log,check=True)
(b/'encoding.json').write_text(json.dumps({'scope':'Local throughput/CPU benchmark only; no per-frame latency, network transport, client decode or concurrent-emulator timing. Source is actual recorded browser motion resampled from25 to30fps, not30distinct guestframes.','rawBytes':raw.stat().st_size,'trials':rows,'decodePassed':True},indent=2)+'\n');print(json.dumps(rows,indent=2));raw.unlink()
