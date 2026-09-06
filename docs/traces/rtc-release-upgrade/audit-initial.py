from pathlib import Path
import json,hashlib,copy
p=Path(__file__).resolve().parent;r=json.loads((p/'run-1/report.json').read_text())
def validate(r):
 assert r['result']=='pass' and r['browserClosed'] and len(r['checks'])==30 and not r['errors']
 assert r['variants']['A']['hashes']['dist/emu.wasm']=='b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606'
 assert r['variants']['B']['hashes']['dist/emu.wasm']=='7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088'
 assert r['variants']['B']['hashes']['seed-jit.js']=='3c2cc4122a9b32ab22db44d253e26920650c885c1c019b170a2951002ce597e9'
 assert r['savedBefore']==r['savedAfter']
 receipts=[]
 for b in r['barriers']:
  assert not b['listening'] and b['refusal']['code']=='ECONNREFUSED' and b['refusal']['port']==8202
  assert not [q for q in r['originRequests'] if q['phase']==b['phase']]
  remote=b['probes']['external']['url']
  for kind,suffix in [('page',''),('dedicated worker','-worker'),('service worker','-sw')]:
   matches=[f for f in r['requestFailures'] if f['phase']==b['phase'] and f['url']==remote+suffix]
   assert len(matches)==1 and matches[0]['failure']['errorText'] in ['net::ERR_INTERNET_DISCONNECTED','net::ERR_CONNECTION_REFUSED']
   assert not [f for f in r['browserResponses'] if f['url']==remote+suffix]
   receipts.append({'phase':b['phase'],'context':kind,**matches[0]})
  assert 'Failed to parse URL' in b['probes']['worker'][0]['error'] # explicitly discarded, never accepted as transport
 offline=[x for x in r['browserResponses'] if x['phase'] in ['disconnected-B','corrupt-kernel-offline']]
 assert len(offline)==48 and all(x['fromServiceWorker'] for x in offline)
 assert not any('files.garden' in x['url'] for x in offline)
 for engine in ['previousEngine','activatedEngine','disconnectedEngine','recoveredEngine']:
  assert r[engine]['enabled'] and r[engine]['retired']>0 and r[engine]['calls']>0
 return receipts
receipts=validate(r);negative=[]
for receipt in receipts:
 altered=copy.deepcopy(r);altered['requestFailures']=[f for f in altered['requestFailures'] if not(f['phase']==receipt['phase'] and f['url']==receipt['url'])]
 try:validate(altered);raise RuntimeError('Missing network receipt was accepted')
 except AssertionError:negative.append({'removed':receipt['url'],'rejected':True})
a=copy.deepcopy(r);i=next(i for i,x in enumerate(a['browserResponses']) if x['phase']=='disconnected-B');a['browserResponses'][i]['fromServiceWorker']=False
try:validate(a);raise RuntimeError('Network response was accepted')
except AssertionError:negative.append({'injectedNonSWOfflineResponse':True,'rejected':True})
source=json.loads((p/'source-pins.json').read_text());assert all(hashlib.sha256((p/g/n).read_bytes()).hexdigest()==h for g,m in source.items() for n,h in m.items())
result={'passed':True,'scriptChecks':30,'independentAbsoluteRemoteTransportReceipts':receipts,'physicalLocalBarriers':r['barriers'],'offlineSWResponses':48,'offlineNetworkResponses':0,'all56FrozenSourcePinsUnchanged':True,'negativeControls':negative,'excludedDiagnostic':'Both redundant Blob-worker relative local URLs failed parsing; not transport proof. Physical TCP refusal closes local-origin gap. Actual absolute remote probes are independently bound to network-error receipts in all three execution contexts. Original report and executed harness unchanged.'}
(p/'run-1/independent-audit.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS independent transport/source audit,7negativecontrols')
