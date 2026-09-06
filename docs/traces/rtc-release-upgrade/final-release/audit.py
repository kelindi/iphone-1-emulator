#!/usr/bin/env python3
"""Artifact-only audit; no emulator or mutations to observed artifacts.
Usage: audit.py EVIDENCE_DIRECTORY [FROZEN_SITE CURRENT_WEB]
"""
import hashlib,json,sys
from pathlib import Path
from urllib.parse import urlparse
base=Path(sys.argv[1]); r=json.loads((base/'report.json').read_text()); pins=json.loads((base/'source-pins.json').read_text())
hashfile=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert r['passed'] and r['browserClosed'] and not r['errors']
assert len(r['checks'])==21 and all(c['passed'] for c in r['checks'])
assert r['savedBefore']==r['savedAfter']==r['savedOffline']
assert r['barrier']['listening'] is False and r['barrier']['refusal']['code']=='ECONNREFUSED'
assert len(r['originRequests'])==r['originCountAtBarrier']
assert len(r['nonce'])==2
for n in r['nonce']:
 matches=[f for f in r['failures'] if f['url']==n['url']]
 assert len(matches)==1 and matches[0]['failure']['errorText']=='net::ERR_INTERNET_DISCONNECTED'
 assert urlparse(n['url']).scheme in ('http','https')
offline=[x for x in r['responses'] if x['phase']=='offline' and urlparse(x['url']).scheme in ('http','https')]
assert len(offline)==16 and all(x['fromServiceWorker'] for x in offline)
for group in ['onlineHashes','installedHashes','offlineHashes']:
 for item in r[group]:assert item['status']==200 and item['sha256']==pins[item['name']]
assert len(pins)==28
if len(sys.argv)>2:
 for tree in map(Path,sys.argv[2:]):
  for name,sha in pins.items():assert hashfile(tree/name)==sha,(tree,name)
for name in ['04-autosave-reload','06-final-offline-running']:
 a=(base/'03-real-autosave.rgba').read_bytes();b=(base/(name+'.rgba')).read_bytes()
 assert len(a)==len(b)==320*480*4
 for channel in range(3):assert a[20*320*4+channel::4]==b[20*320*4+channel::4]
assert hashfile(base/'smoke.mjs')=='fb077d5b0c79d397946d7340625db01dfbcbf38e1c1d9e38147e7a32b83a79c3'
print(json.dumps({'passed':True,'browserChecks':21,'offlineHTTPFromSW':16,'RGBpixelsCompared':294400,'sourceTreesVerified':sys.argv[2:]}))
