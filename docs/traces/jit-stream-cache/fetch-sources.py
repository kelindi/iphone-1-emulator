#!/usr/bin/env python3
"""Fetch primary pinned source files and reject any unexpected content."""
import base64,hashlib,json,pathlib,sys,urllib.request
here=pathlib.Path(__file__).resolve().parent
out=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '.context/jit-stream-cache/source-refetch')
for entry in json.loads((here/'source-manifest.json').read_text()):
 data=base64.b64decode(urllib.request.urlopen(entry['url']).read())
 assert len(data)==entry['bytes'] and hashlib.sha256(data).hexdigest()==entry['sha256'],entry['path']
 path=out/entry['path'];path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
 print(entry['sha256'],entry['path'])
