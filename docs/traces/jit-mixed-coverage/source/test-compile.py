from pathlib import Path
import runpy,json
analyze=runpy.run_path('.context/jit-mixed-coverage/analyze-compile.py')['analyze']
markers=[{'name':'clock_sync','ts':100,'args':{'sync_id':'navigation-start'}},{'name':'clock_sync','ts':1000,'args':{'sync_id':'fixed-work-end'}}]
def event(ts=100,dur=500,tts=100,tdur=300,pid=1,tid=1,name='wasm.ExecuteCompilationUnits'):return dict(name=name,ph='X',ts=ts,dur=dur,tts=tts,tdur=tdur,pid=pid,tid=tid)
a=analyze(markers+[event(),event(ts=200,dur=100,tts=200,tdur=50,name='wasm.CompileBaseline')]);assert a['cpuLowerMs']==.3 and a['cpuUpperMs']==.3 # no nested double-count
b=analyze(markers+[event(),event(pid=2),event(tid=2)]);assert b['cpuLowerMs']==.9 # background and other processes included
c=analyze(markers+[event(ts=50,dur=100)]);assert c['cpuLowerMs']==0 and c['cpuUpperMs']==.3 # unknown overlap bounded
d=event();del d['tts'];assert analyze(markers+[d])['missingThreadFields']==1
for invalid in [markers,markers[1:]+[event()],markers[:1]+[event()]]:
 try:analyze(invalid)
 except (KeyError,AssertionError):pass
 else:raise AssertionError('missing evidence accepted')
Path('.context/jit-mixed-coverage/compile-preflight.json').write_text(json.dumps({'passed':True,'checks':7,'cases':['nested exclusion','all threads/processes inclusion','crossing bounds','missing CPU fields','empty scopes','missing start','missing end']},indent=2))
