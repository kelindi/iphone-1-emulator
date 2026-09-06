"""Fetch and hash-check the exact primary source files used in this review."""
from pathlib import Path
import urllib.request,base64,json,hashlib,sys
here=Path(__file__).resolve().parent
revision=json.loads((here/'revision.json').read_text())['commit']
output=Path(sys.argv[1] if len(sys.argv)>1 else '.context/v8-profiler-review-reproduced')
for row in json.loads((here/'sources.json').read_text()):
 if 'error' in row:continue
 url='https://chromium.googlesource.com/v8/v8/+/'+revision+'/'+row['path']+'?format=TEXT'
 source=base64.b64decode(urllib.request.urlopen(url,timeout=30).read())
 assert hashlib.sha256(source).hexdigest()==row['sha256'],row['path']
 target=output/row['path'];target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(source)
print('All pinned primary source hashes match')
