from pathlib import Path
import subprocess,os,shutil,json,time
base=Path('.context/jit-mixed-coverage');p=base/'measured';paths=[p/n/'profile/Default'/c for n in ['1-original','2-candidate','3-candidate']for c in ['Cache','Code Cache']];ps=subprocess.check_output(['ps','-eo','pid,ppid,args'],text=True)
for line in ps.splitlines():
 if 'chrome'in line.lower()and'--user-data-dir='in line:assert not any(str(x.parent.parent)in line for x in paths),line
free=lambda:os.statvfs('.').f_bavail*os.statvfs('.').f_frsize
r={'scope':'Root authorized completed3 HTTP/Code caches only, no IDB/CacheStorage/traces/settings removed. Profiles no longer eligible for native-cache timing reuse.','beforeFree':free(),'paths':[{'path':str(x),'allocatedBytes':int(subprocess.check_output(['du','-sB1',str(x)],text=True).split()[0])}for x in paths],'processReceipt':ps,'started':time.time()};dest=base/'fourth-cleanup.json';dest.write_text(json.dumps(r,indent=2))
for x in paths:assert x.name in ['Cache','Code Cache']and x.parent.name=='Default';shutil.rmtree(x)
os.sync();r['afterFree']=free();r['finished']=time.time();dest.write_text(json.dumps(r,indent=2));print('reclaimed requested',sum(x['allocatedBytes']for x in r['paths']),'free',r['afterFree'])
