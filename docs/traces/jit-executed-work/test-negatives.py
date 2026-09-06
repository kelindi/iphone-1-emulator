from pathlib import Path
import subprocess,json,os
p=Path('.context/jit-executed-work').resolve();source=(p/'compiler.cjs').read_text();h=(p/'test-boundaries.cjs').read_text()
# Independent guest state equality is checked before deliberate observer errors.
h=h.replace('let observer=null;', '''if(engine){assert.equal(ran,reference.ran);assert(bytes.equals(reference.bytes),'Negative counter mutation changed guest state');fs.writeFileSync(root+'guest-state-before-count-assertion.json',JSON.stringify({passed:true,label,engine,count,stateSha256:crypto.createHash('sha256').update(bytes).digest('hex')}));}let observer=null;''')
(p/'negative-boundaries.cjs').write_text(h)
mutants={
 'skipped-retirement':source.replace('+censusCommit(e,c);',';'),
 'wrong-pc':source.replace('const slot=e.index*512+offset/width,','const slot=e.index*512+offset/width+1,'),
 'whole-block-credit':source.replace("for(let bi=lo;bi<hi;bi++){const b=blocks[bi];body+=`) `;", "for(let bi=lo;bi<hi;bi++){const b=blocks[bi];body+=`) `;body+=b.ops.map(c=>censusCommit(e,c)).join('');")
}
rows=[]
for name,s in mutants.items():
 assert s!=source;file=p/(name+'.cjs');file.write_text(s);out=p/('negative-'+name);out.mkdir(exist_ok=True)
 with (p/(name+'.log')).open('w') as log:
  result=subprocess.run(['node',str(p/'negative-boundaries.cjs')],env=dict(os.environ,ORACLE_MODULE=str(p/'observer-oracle.cjs'),COMPILER_PATH=str(file),ARTIFACT_DIR=str(out)),stdout=log,stderr=subprocess.STDOUT,timeout=60)
 text=(p/(name+'.log')).read_text();assert result.returncode!=0,name
 assert 'Generated per-PC counts must conserve' in text or 'Per-PC/opcode completed-work footprint' in text,name
 state=json.loads((out/'guest-state-before-count-assertion.json').read_text());assert state['passed']
 rows.append({'name':name,'rejected':True,'architectureUnchangedBeforeCountFailure':state,'exitCode':result.returncode})
(p/'negative-tests.json').write_text(json.dumps({'passed':True,'cases':rows},indent=2)+'\n');print(json.dumps(rows))
