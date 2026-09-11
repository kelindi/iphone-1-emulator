from pathlib import Path
import shutil,os
p=Path('.context/seed-gap')
for arm in ['control','candidate']:
 site=p/(arm+'-site');shutil.copytree('web',site,copy_function=os.link,ignore=shutil.ignore_patterns('tests'),dirs_exist_ok=True)
 if arm=='candidate':
  for f in ['seed.wasm','regions.json','abi.json']:
   t=site/'jit'/f;t.unlink();shutil.copy2(p/'seed'/f,t)
shutil.copy2('.context/connected-engine/performance.mjs',p/'performance.mjs')
s=Path('.context/core-hotspot/browser.py').read_text();a=s.index("# Maintained harness");b=s.index("for i,arm in enumerate(report['order']):")
s=s[:s.index("for arm in ['control','candidate']:")]+s[b:]
s=s.replace('.context/core-hotspot','.context/seed-gap').replace("EXPECT_CORE=str(p/arm/'emu.wasm')","EXPECT_CORE='web/dist/emu.wasm',EXPECT_SEED=str(p/'seed/seed.wasm') if arm=='candidate' else 'web/jit/seed.wasm'")
(p/'browser.py').write_text(s)
s=Path('.context/core-hotspot/analyze.py').read_text().replace('.context/core-hotspot','.context/seed-gap')
a=s.index("'scope':");b=s.index(",'summary':",a)
s=s[:a]+"'scope':'Same production CPU and unchanged compiler, original versus256-gap seed; control/candidate/candidate/control;11pairs/session,20warm samples/action/version. Normal clock/autosave. Readiness50ms sample and500ms confirmation. Separate fixed replay validates state; timing is cloudChrome, notMac or all-app acceptance.'"+s[b:]
(p/'analyze.py').write_text(s)
s=Path('.context/core-hotspot/review-frames.py').read_text().replace('.context/core-hotspot','.context/seed-gap');(p/'review-frames.py').write_text(s)
