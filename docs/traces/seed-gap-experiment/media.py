from pathlib import Path
import json,subprocess
p=Path('.context/seed-gap');f=str(Path('node_modules/ffmpeg-static/ffmpeg').resolve());r=json.loads((p/'frozen-1-candidate/report.json').read_text());out='tweets/videos/seed_gap_comparison.mp4'
with (p/'encode.log').open('w')as log:subprocess.run([f,'-y','-threads','1','-i',r['video'],'-c:v','libx264','-threads','1','-preset','fast','-crf','23','-pix_fmt','yuv420p',out],stdout=log,stderr=log,check=True)
with (p/'thumbnail.log').open('w')as log:subprocess.run([f,'-y','-threads','1','-ss','20','-i',out,'-frames:v','1','-threads','1','tweets/thumbnails/seed_gap_comparison.jpg'],stdout=log,stderr=log,check=True)
with (p/'decode.log').open('w')as log:subprocess.run([f,'-v','error','-threads','1','-i',out,'-f','null','-'],stdout=log,stderr=log,check=True)
