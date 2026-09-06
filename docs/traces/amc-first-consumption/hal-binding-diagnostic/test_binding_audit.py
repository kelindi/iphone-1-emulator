"""Synthetic independent descriptor/fault classification tests; no guest."""
from pathlib import Path
import importlib.util
s=importlib.util.spec_from_file_location('a',Path(__file__).with_name('binding-audit.py'));a=importlib.util.module_from_spec(s);s.loader.exec_module(a)
checks=0
va=0x31236650;cp=[1|(1<<23),0x10000000,0,0,1,0,0,0,0,0];l1=0x10000000+(va>>20)*4;l2=0x10004000+((va>>12)&255)*4
for access,fresh,value,fault,pa in [(0,True,0,7,0),(2,True,0,7,0),(0,True,0x10136ff2,0,0x10136650),(2,True,0x10136ff3,15,0)]:
 j={'cp15':cp};w={'access':access,'freshClone':fresh,'spans':[[va,32,pa,fault,int(not fault)]],'tables':[[l1,0x10004001,1],[l2,value,1]]};a.fresh_walk(j,w);checks+=1
 wrong=dict(w);wrong['spans']=[[va,32,pa,fault^1,int(not fault)]]
 try:a.fresh_walk(j,wrong);raise RuntimeError('wrong fault accepted')
 except AssertionError as e:assert 'fault kind' in str(e);checks+=1
 wrong=dict(w);wrong['tables']=[[l1^4,0x10004001,1],[l2,value,1]]
 try:a.fresh_walk(j,wrong);raise RuntimeError('wrong table accepted')
 except AssertionError as e:assert 'provenance' in str(e);checks+=1
print(f'PASS {checks} synthetic independent binding-table/fault controls; no guest')
