from pathlib import Path
import sys,json,gzip,hashlib
from decode import decode
p=Path(sys.argv[1]);meta=json.loads((p/'capture.json').read_text());meta['positions']=json.loads(Path(meta['positionsPath']).read_text())
rows=gzip.decompress((p/'rows.bin.gz').read_bytes());generated=gzip.decompress((p/'generated.bin.gz').read_bytes());summary=gzip.decompress((p/'summary.bin.gz').read_bytes())
try:
 r=decode(rows,generated,summary,meta)
 out={k:v for k,v in r.items()if k not in ['interpreted','generated','work']};out['distinctWorkKeys']=len(r['work']);out['generatedPositions']=len(r['generated']);out['rawHashes']={k:hashlib.sha256(v).hexdigest()for k,v in [('rows',rows),('generated',generated),('summary',summary)]}
 (p/'decoded-summary.json').write_text(json.dumps(out,indent=2)+'\n')
except BaseException as e:
 (p/'decode-failure.json').write_text(json.dumps({'passed':False,'error':str(e),'type':type(e).__name__},indent=2)+'\n');raise
