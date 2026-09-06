from pathlib import Path
import shutil,json,hashlib
base=Path('.context/guest-http-default');dest=Path('docs/traces/guest-http-default')
files=[]
for pat in ['*.py','*.c','*.mjs','*.json','qualification-plan.md','build-quiet-2/*.json','checks/*.json','checks/*.py','checks/*.log','installed/*.json','native-run-1/*.json','native-run-1/*.log','native-run-1/process-health/*.json','native-run-1/frame.17.png','browser-run-1/*.json','browser-run-1/*.log','browser-run-1/*.png','browser-run-1/failed-form-process-health/*.json','browser-run-1/guest-log-audit/*.json','browser-run-1/guest-log-audit/extract.stderr']:
 files+=list(base.glob(pat))
for p in sorted(set(files)):
 q=dest/p.relative_to(base);q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
for name in ['0.bin','4.bin','5.bin','6.bin','7.bin']:
 p=base/'browser-run-1/guest-log-audit'/name;q=dest/p.relative_to(base);shutil.copyfile(p,q)
for p in Path('tools/legacy_services/guest_http').iterdir():
 if p.is_file() and p.suffix in ['.c','.h','.py','.md','.plist','.txt']:
  q=dest/'maintained-source'/p.name;q.parent.mkdir(exist_ok=True);shutil.copyfile(p,q)
r=json.loads((base/'browser-run-1/report.json').read_text());serial=(base/'browser-run-1/serial.log').read_text()
a={'workflowPassed':False,'elapsedSeconds':r['elapsedSeconds'],'passedScriptChecks':len(r['checks']),'errors':r['errors'],'consoleCount':len(r['console']),'browserRequestFailures':len(r['requestFailures']),'receivedAssetAudit':r['responseAudit'],'projectionUnchanged':r['projectionUnchanged'],'finalState':r['finalState'],'failedSnapshot':r['exportDiagnostic'],'graphicsPending2DHangs':serial.count('Graphics hang with pending 2D blit'),'graphics3DRestarts':serial.count('Graphics Restart during 3D Blit'),'daemonFailureLines':[line for line in serial.splitlines() if any(s in line for s in ['LOCAL_HTTP_FAIL','HTTP_REQUEST_REJECTED','send_error','http_read_error','HTTP_BUILD_FAILED','listener_poll_error','close_server'])],'scope':'Artifact-only audit. Seven passed checks are partial; form/bookmark/autosave-reload/final Home were not accepted. Generic final-home.snap contains failed form. Daemon absent at final snapshot; cause unobserved. No speed claim.'}
(dest/'acceptance-audit.json').write_text(json.dumps(a,indent=2)+'\n')
print(json.dumps(a,indent=2)[:1000])
