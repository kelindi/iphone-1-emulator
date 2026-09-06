from pathlib import Path
old=Path('.context/jit-attribution/profile-browser.mjs').read_text()
observer=old[old.index(' await page.evaluate(()=>{'):old.index(' const box=await')]
Path('.context/first-use/observer.js').write_text(observer)
worker=Path('web/emu-worker.js').read_text()
assert worker.count('const ran = api.run(50000);')==1
worker='let diagnosticRetired = 0;\n'+worker.replace('const ran = api.run(50000);','const ran = api.run(50000); diagnosticRetired += ran;')
worker=worker.replace("case 'idle':", "case 'profileCounters': send('profileCounters', {id:message.id, retired:diagnosticRetired, ticks:api.ticks(), cycles:String(api.cycles()), jit:jit?.stats(), at:performance.now()}); return;\n      case 'idle':")
Path('.context/first-use/emu-worker.js').write_text(worker)
app=Path('web/app.js').read_text()
assert app.count('setInterval(() => save(false), 30000);')==1
Path('.context/first-use/app.js').write_text(app.replace('setInterval(() => save(false), 30000);','/* Diagnostic only: automatic saves suppressed identically for all stages. */'))
