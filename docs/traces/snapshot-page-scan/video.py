from pathlib import Path
import subprocess,re,json
ff='node_modules/ffmpeg-static/ffmpeg'
p=Path('.context/snapshot-page-scan')
a=Path(json.loads((p/'browser-1-baseline/report.json').read_text())['video']);b=Path(json.loads((p/'browser-2-candidate/report.json').read_text())['video'])
def duration(p):
 s=subprocess.run([ff,'-hide_banner','-i',str(p)],capture_output=True,text=True).stderr
 h,m,s=map(float,re.search(r'Duration: (\d+):(\d+):([\d.]+)',s).groups());return h*3600+m*60+s
ad,bd=duration(a),duration(b)
out='tweets/videos/snapshot_page_scan.mp4'
f="[0:v]drawtext=text='Original capture - Save and Pause':x=20:y=20:fontsize=24:fontcolor=white:box=1:boxcolor=black@0.8:boxborderw=8,setsar=1[a];[1:v]drawtext=text='Faster capture - same snapshot bytes':x=20:y=20:fontsize=24:fontcolor=white:box=1:boxcolor=black@0.8:boxborderw=8,setsar=1[b];[a][b]concat=n=2:v=1:a=0[v]"
subprocess.run([ff,'-hide_banner','-loglevel','error','-i',str(a),'-i',str(b),'-filter_complex_threads','1','-filter_complex',f,'-map','[v]','-c:v','libx264','-threads','1','-preset','fast','-crf','22','-pix_fmt','yuv420p','-movflags','+faststart','-y',out],check=True)
subprocess.run([ff,'-hide_banner','-loglevel','error','-ss',str(ad+12),'-i',out,'-frames:v','1','-q:v','2','-y','tweets/thumbnails/snapshot_page_scan.jpg'],check=True)
(p/'video.json').write_text(json.dumps({'sourceBaseline':str(a),'sourceCandidate':str(b),'baselineSeconds':ad,'candidateSeconds':bd,'totalSeconds':ad+bd,'thumbnailSeconds':ad+12,'output':out,'playback':'Original speed, full first accepted ordered pair, labels only; no omitted or accelerated sections.'},indent=2)+'\n')
print(ad,bd,ad+bd)
