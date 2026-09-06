from pathlib import Path
import re, subprocess, json, hashlib
base=Path('.context/rtc-second'); report={'passed':False,'commands':[]}
def run(args, label, expect=0):
    r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    (base/(label+'.log')).write_text(r.stdout)
    report['commands'].append({'command':args,'exitCode':r.returncode,'log':label+'.log'})
    (base/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    assert r.returncode==expect,(label,r.returncode)
source=Path('core/src/soc/pcf50635.c')
names=re.findall(r'^\w+ (s5l_pcf50635_\w+)\(',source.read_text(),re.M)
assert 's5l_pcf50635_tick' in names
rename=['-D'+n+'='+('baseline_tick' if n=='s5l_pcf50635_tick' else 'baseline_'+n) for n in names]
common=['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Icore/include']
run(common+rename+['-c',str(source),'-o',str(base/'baseline.o')],'baseline-build')
run(common+[str(base/'test.c'),str(base/'pcf50635.c'),str(base/'baseline.o'),'-o',str(base/'test')],'test-build')
run([str(base/'test')],'test')
# The unchanged model must fail the first missing-event contract.
run(common+[str(base/'test.c'),str(source),str(base/'baseline.o'),'-o',str(base/'test-missing-event')],'negative-build')
r=subprocess.run([str(base/'test-missing-event')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(base/'negative.log').write_text(r.stdout)
assert r.returncode==1 and 'p.regs[PCF50635_INT1]==0x80' in r.stdout
report['missingEventNegative']={'exitCode':r.returncode,'expectedFailure':True}
archive=Path('.context/build-native/core/libemucore.a')
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='0a02dbd345758710c8f635487abafd19b2b1597ea32fb2b1c60fd1526cd4aec2'
run(['cc','-std=c11','-O3','-Icore/include','-I.context/rtc-contract','-I.context/amc-first/stage-g/native-include','-I.context/amc-first/stage-h/source',str(base/'probe.c'),str(base/'pcf50635.c'),'.context/amc-first/stage-h/source/peek.c',str(archive),'-lm','-o',str(base/'probe')],'probe-build')
files=[base/'probe.c',base/'pcf50635.c',base/'test.c',base/'build.py',base/'run.py',base/'audit.py',base/'probe',archive,Path('.context/rtc-contract/emu_api.c'),Path('.context/amc-first/stage-h/source/peek.c'),Path('.context/amc-first/stage-h/source/peek.h'),Path('.context/rtc-contract/browser-2/before-manual-change.snap')]
report['pins']={str(p):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files}
report['passed']=True
(base/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'passed':True,'contracts':(base/'test.log').read_text(),'missingEventNegative':report['missingEventNegative']}))
