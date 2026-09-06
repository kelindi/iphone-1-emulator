import gzip,hashlib,json,pathlib
r=pathlib.Path('.context/jit-fallback-census');defs_path=r/'site/jit/regions.json';defs=json.loads(defs_path.read_text());regions={(d['pc'],int(d['thumb'])):d for d in defs};assert len(regions)==len(defs)==512
report={'scope':'Independent static admission reconstruction from original seed definitions and retained actual browser rows. Inactive versus active positions are combined; this does not independently validate their runtime invalidation history.','regionsSHA256':hashlib.sha256(defs_path.read_bytes()).hexdigest(),'actions':[]}
def allowed(row):
 key=(row['pc']&~1023,int(bool(row['flags']&256)));d=regions.get(key)
 if d is None:return {1} if (key[0],1-key[1]) in regions else {0}
 pos=(row['pc']%1024)//(2 if key[1] else 4)
 return {3,4} if (d['masks'][pos//32]>>(pos%32))&1 else {2}
for name in ['warm-settings','warm-home']:
 p=r/'browser-1'/f'{name}-rows.json.gz';rows=json.loads(gzip.decompress(p.read_bytes()));totals=[0]*5
 for row in rows:
  assert row['admission'] in allowed(row),(name,row)
  totals[row['admission']]+=row['count']
 actual=next(a for a in json.loads((r/'browser-1/report.json').read_text())['actions'] if a['label']==name)
 assert totals==actual['admission'];assert sum(totals)==actual['after']['census']['calls']
 negatives=0
 for category in [0,1,2,3,4]:
  row=next(x for x in rows if x['admission']==category);bad=dict(row);bad['admission']=next(n for n in range(5) if n not in allowed(row));assert bad['admission'] not in allowed(bad);negatives+=1
 report['actions'].append({'name':name,'rawRowsSHA256':hashlib.sha256(p.read_bytes()).hexdigest(),'rows':len(rows),'totals':totals,'allStaticAdmissionChecksPass':True,'alteredAdmissionControlsRejected':negatives})
(r/'root-admission-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
