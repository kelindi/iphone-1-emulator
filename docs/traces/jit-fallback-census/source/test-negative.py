from pathlib import Path
import subprocess,json
p=Path(__file__).resolve().parent
source=(p/'census.c').read_text()
cases=[('missing-fetch','current.flags|=FC_FETCH;','current.flags|=0;', 'test-collector.c',None),('wrong-admission','?FC_NO_ADMISSION:!s->active','?FC_ADMITTED:!s->active','test-collector.c',None),('hidden-overflow','total.overflow++;','/* omitted overflow */','test-overflow.c',8)]
results=[]
for name,old,new,test,cap in cases:
 assert source.count(old)==1,(name,source.count(old))
 src=p/(name+'.c');src.write_text(source.replace(old,new));binary=p/name
 cmd=['cc','-std=c11','-O1','-Wall','-Wextra','-Werror',str(src),str(p/test),'-o',str(binary)]
 if cap:cmd.insert(1,'-DFC_CAPACITY='+str(cap))
 subprocess.run(cmd,check=True)
 r=subprocess.run([str(binary)],capture_output=True,text=True)
 (p/(name+'.log')).write_text(r.stdout+r.stderr)
 assert r.returncode!=0,(name,'negative incorrectly accepted')
 results.append(dict(name=name,returncode=r.returncode,expectedRejection=True,stdout=r.stdout,stderr=r.stderr))
(p/'negative-report.json').write_text(json.dumps(results,indent=2)+'\n')
print('all three deliberate collector corruptions rejected')
