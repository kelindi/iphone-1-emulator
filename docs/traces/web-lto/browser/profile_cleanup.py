from pathlib import Path
import os,json,shutil,hashlib
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def archive_profile(folder, allowed_root):
 assert folder.resolve().parent==allowed_root.resolve() and not folder.is_symlink(), "Not a new owned arm directory"
 profile=folder/'profile';assert profile.is_dir()and not profile.is_symlink()and profile.resolve()==folder.resolve()/'profile'
 inventory=[]
 for f in profile.rglob('*'):
  if f.is_file()and not f.is_symlink():inventory.append({'path':str(f.relative_to(profile)),'bytes':f.stat().st_size})
 record={'scope':'New successful disposable profile only; all scoped processes alreadyclosed. Entire IndexedDB,LocalStorage,SessionStorage and Preferences retained outside profile. HTTP/Code caches not reused or needed for this fresh-profile comparison. No oldprofile/fixture mutated.','freeBefore':shutil.disk_usage('.').free,'inventory':inventory,'preserved':[]}
 preserved=folder/'saved-profile';preserved.mkdir()
 for relative in ['Default/IndexedDB','Default/Local Storage','Default/Session Storage','Default/Preferences','Local State']:
  src=profile/relative
  if not src.exists():continue
  dst=preserved/relative;dst.parent.mkdir(parents=True,exist_ok=True);os.replace(src,dst);record['preserved'].append(relative)
 record['savedFileHashes']={str(f.relative_to(preserved)):{'bytes':f.stat().st_size,'sha256':sha(f)}for f in preserved.rglob('*')if f.is_file()and not f.is_symlink()}
 assert 'Default/IndexedDB'in record['preserved'],'Expected import/save IndexedDB evidence missing'
 (folder/'profile-cleanup.json').write_text(json.dumps(record,indent=2));shutil.rmtree(profile);record['removed']=not profile.exists();record['freeAfter']=shutil.disk_usage('.').free;(folder/'profile-cleanup.json').write_text(json.dumps(record,indent=2));return record
