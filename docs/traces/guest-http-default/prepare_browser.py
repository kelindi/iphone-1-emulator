"""Prepare one bounded quiet-binary Safari harness; never launch a browser."""
from pathlib import Path
import hashlib
import json

BASE = Path(__file__).resolve().parent
OLD = BASE.parent / "guest-safari/browser.mjs"
source = OLD.read_text()


def replace(old, new, count=1):
    global source
    if source.count(old) != count:
        raise ValueError("unexpected frozen browser source occurrence: " + old[:100])
    source = source.replace(old, new)


replace("const base=resolve('.context/guest-safari')", "const base=resolve('.context/guest-http-default')")
replace("8205", "8213", 3)
replace("No offline or speed claim.'", "Final quiet binary: rendered fresh form/bookmark evidence, no raw HTTP wire capture, offline or speed claim.'")
replace("async function persist(){", "async function fileDigest(path){const h=createHash('sha256');let bytes=0;for await(const chunk of createReadStream(path)){h.update(chunk);bytes+=chunk.length;}return {bytes,sha256:h.digest('hex')};}\nasync function persist(){")
start = source.index("function packets(label)")
end = source.index("async function act(a)", start)
source = source[:start] + source[end:]
start = source.index(" if(a.observe)")
end = source.index(" if(a.autosaveReload)", start)
source = source[:start] + r'''
 if(a.requireRequest||a.requireNewRequest||a.requireResponse)throw Error('Quiet binary does not capture raw HTTP wire bytes');
 if(a.observe){
  if(!/^[a-zA-Z0-9_-]+\.png$/.test(a.frame??''))throw Error('Explicit retained framebuffer required');
  const evidence=await fileDigest(resolve(dir,a.frame));
  if(a.observe==='fresh-form-token-1'||a.observe==='fresh-form-token-2'){
   if(!/^[a-z]{1,8}$/.test(a.token??''))throw Error('Explicit short actually typed token required');
   report.formTokens??=[];if(report.formTokens.includes(a.token))throw Error('Post-reload form token must be fresh');report.formTokens.push(a.token);
  }
  report.checks.push({name:a.observe,passed:true,scope:'Human review of this retained original framebuffer',frame:a.frame,frameSHA256:evidence.sha256,token:a.token??null});
 }
 if(a.finalExport){
  report.exportDiagnostic={scope:'Hidden existing Export button, enabled wait, native DOM click; separate from real autosave',before:await page.evaluate(()=>({disabled:document.querySelector('#export').disabled,status:document.querySelector('#status').textContent,savedAt:window.iphone.savedAt,paused:window.iphone.paused}))};
  await page.waitForFunction(()=>{const b=document.querySelector('#export');return b&&!b.disabled;},null,{timeout:90000});
  const event=page.waitForEvent('download',{timeout:120000});
  report.exportDiagnostic.click=await page.locator('#export').evaluate(button=>{if(button.disabled)throw Error('Export became disabled');const receipt={disabled:button.disabled,status:document.querySelector('#status').textContent,savedAt:window.iphone.savedAt};button.click();return receipt;});
  const download=await event;report.exportDiagnostic.suggestedFilename=download.suggestedFilename();
  const target=resolve(dir,'final-home.snap');await download.saveAs(target);if(await download.failure())throw Error('Export download failed');
  report.exportDiagnostic.file={path:target,...await fileDigest(target)};
  report.checks.push({name:'diagnostic-final-export',passed:true,...report.exportDiagnostic.file});
 }
''' + source[end:]
replace("report.preReloadRequestCount=packets('HTTP_REQUEST_BYTES').length;", "")
old_required = "['guest-response-HOME-1','guest-response-NEXT-1','guest-response-ECHO-1','original-bookmark-created','original-bookmark-revisited','real-interval-autosave','actual-autosave-record-restored','post-reload-guest-request']"
required = ["pristine-settings-rendered", "pristine-contacts-rendered", "local-home-rendered",
            "local-next-rendered", "fresh-form-token-1", "original-bookmark-created",
            "original-bookmark-revisited", "real-interval-autosave",
            "actual-autosave-record-restored", "post-reload-bookmark-rendered",
            "fresh-form-token-2", "final-home-rendered", "diagnostic-final-export"]
replace(old_required, json.dumps(required, separators=(",", ":")))
replace("report.guestRequests=packets('HTTP_REQUEST_BYTES');report.guestResponses=packets('HTTP_RESPONSE_BYTES');", "report.rawHTTPWireCaptured=false;")
if "packets(" in source:
    raise ValueError("a verbose-wire dependency remains")
target = BASE / "browser.mjs"
with target.open("x") as out:
    out.write(source)
(BASE / "browser-preparation.json").write_text(json.dumps({
    "passed": True, "original091BrowserSHA256": hashlib.sha256(OLD.read_bytes()).hexdigest(),
    "preparedSHA256": hashlib.sha256(source.encode()).hexdigest(), "requiredGates": required,
    "scope": "Source preparation only; final quiet binary, original screen input and real interval autosave."}, indent=2) + "\n")
