from pathlib import Path
from tempfile import TemporaryDirectory
from profile_cleanup import archive_profile
import hashlib,json
checks=0
with TemporaryDirectory(prefix='lto-cleanup-contract-')as temp:
 root=Path(temp);owned=root/'owned';owned.mkdir();arm=owned/'1-original';p=arm/'profile'
 (p/'Default/IndexedDB/a.blob').mkdir(parents=True);(p/'Default/Cache').mkdir();(p/'Default/Code Cache').mkdir();(p/'Default/Local Storage').mkdir();
 raw=bytes(range(256))*4;(p/'Default/IndexedDB/a.blob/value').write_bytes(raw);(p/'Default/IndexedDB/db.leveldb').write_bytes(b'metadata');(p/'Default/Preferences').write_text('{"owned":true}');(p/'Default/Cache/disposable').write_bytes(b'download');(p/'Default/Code Cache/disposable').write_bytes(b'code');(p/'Default/Local Storage/value').write_bytes(b'local');(arm/'trace.json').write_bytes(b'trace');foreign=root/'outside';foreign.mkdir();(foreign/'sentinel').write_bytes(b'untouched')
 r=archive_profile(arm,owned);assert r['removed']and not p.exists();checks+=1
 assert (arm/'saved-profile/Default/IndexedDB/a.blob/value').read_bytes()==raw;checks+=1
 assert (arm/'saved-profile/Default/IndexedDB/db.leveldb').read_bytes()==b'metadata';checks+=1
 assert (arm/'saved-profile/Default/Local Storage/value').read_bytes()==b'local';checks+=1
 assert (arm/'saved-profile/Default/Preferences').read_text()=='{"owned":true}';checks+=1
 assert (arm/'trace.json').read_bytes()==b'trace'and(foreign/'sentinel').read_bytes()==b'untouched';checks+=1
 assert len(r['savedFileHashes'])==4 and r['savedFileHashes']['Default/IndexedDB/a.blob/value']['sha256']==hashlib.sha256(raw).hexdigest();checks+=1
 other=root/'other';(other/'profile').mkdir(parents=True)
 try:archive_profile(other,owned);raise RuntimeError('foreignaccepted')
 except AssertionError:checks+=1
 linked=owned/'2-candidate';linked.mkdir();(linked/'profile').symlink_to(foreign,target_is_directory=True)
 try:archive_profile(linked,owned);raise RuntimeError('symlinkaccepted')
 except AssertionError:checks+=1
 assert (foreign/'sentinel').read_bytes()==b'untouched';checks+=1
print(json.dumps({'passed':True,'checks':checks,'scope':'Synthetic owned-profile extraction only; IDB/blob/metadata exact, trace and foreign directory preserved; rejects foreign arm/profile symlink. No browser/guest.'}))
