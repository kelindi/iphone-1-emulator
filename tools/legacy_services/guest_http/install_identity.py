"""Provision a coherent own Lockdown identity on a new, explicitly hashed copy."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import tempfile
import install_services as S
H, W = S.H, S.W
PARENT = '/private/var/root/Library/Lockdown'
PRIVATE = PARENT + '/device_private_key.pem'
PUBLIC = PARENT + '/device_public_key.pem'
RECORD = PARENT + '/activation_record.plist'
ACTIVATION = '/System/Library/Lockdown/iPhoneActivation.pem'
CA = '/System/Library/Lockdown/iPhoneDeviceCA.pem'
sha = lambda b: hashlib.sha256(b).hexdigest()

def install(source, target, expected_source, *, device_key=None):
    source, target = Path(source), Path(target)
    receipt = target.with_suffix(target.suffix + '.json')
    if target.exists() or receipt.exists() or source.resolve() == target.resolve():
        raise ValueError('destination must be new')
    if S.digest(source) != expected_source:
        raise ValueError('source hash mismatch')
    before, paths, forks = S.inventory(source)
    if PRIVATE in paths or PUBLIC in paths:
        raise ValueError('identity key destinations must be absent')
    if PARENT not in paths:
        raise ValueError('existing Lockdown parent required')
    parent_data = paths[PARENT][2]
    if H.u16(parent_data, 42) & 0o170000 != 0o040000 or H.u32(parent_data,32) or H.u32(parent_data,36):
        raise ValueError('unexpected Lockdown parent metadata')
    activation, ca, record, keys = W._local_activation_data(device_key=device_key, include_device_keys=True)
    work = W.Work(source)
    try:
        old = plistlib.loads(work.read_file_path(RECORD))
        if not isinstance(old, dict): raise ValueError('activation record must be dictionary')
        merged = dict(old); merged.update(record)
        replacements = {ACTIVATION:activation, CA:ca, RECORD:plistlib.dumps(merged, fmt=plistlib.FMT_XML)}
        for path, data in replacements.items():
            hit = work.find_file_path(path)
            if path not in paths or not hit or H.u16(paths[path][2],42) & 0o170000 != 0o100000:
                raise ValueError('existing regular identity configuration required: ' + path)
            if len(data) > hit[0].total_blocks * work.bs:
                raise ValueError('identity configuration exceeds existing capacity: ' + path)
        if any(plistlib.loads(replacements[RECORD]).get(k) != v for k,v in old.items() if k not in record):
            raise ValueError('unrelated activation keys changed')
    finally: work.vol.f.close()
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='identity-', dir=target.parent) as directory:
        directory = Path(directory)
        additions=[]
        for guest, name, mode in [(PRIVATE,'device_private_key.pem',0o600),(PUBLIC,'device_public_key.pem',0o644)]:
            host=directory/name
            with host.open('xb') as f:
                os.chmod(host,0o600);f.write(keys[name])
            additions.append((guest,host,mode))
        proof = S.preflight(source,additions,expected_source,reuse_catalog_ids=True)
        temporary = directory/'copy.img'
        shutil.copyfile(source,temporary)
        os.chmod(temporary,0o600)
        work = W.Work(temporary,reuse_catalog_ids=True)
        try:
            for guest,host,mode in additions:work.add_file(guest,host,mode=mode,uid=0,gid=0)
            for guest,data in replacements.items():work.replace_file_path(guest,data)
        finally:work.vol.f.close()
        after, newpaths, newforks = S.inventory(temporary)
        if set(newpaths)-set(paths)!={PRIVATE,PUBLIC} or not set(paths)<=set(newpaths) or not set(before)<=set(after):
            raise ValueError('unexpected catalog path changes')
        addedkeys=set()
        for guest,host,mode in additions:
            key,offset,data=newpaths[guest];ident=H.u32(data,8)
            addedkeys.update([key,(ident,'',4)])
            name=Path(guest).name
            thread=W.Work._make_key(ident,'')+struct.pack('>HHIH',4,0,key[0],len(name))+name.encode('utf-16-be')
            if after.get((ident,'',4))!=thread:raise ValueError('unexpected file thread')
            if H.u16(data,42)!=(0o100000|mode) or H.u32(data,32) or H.u32(data,36):raise ValueError('unexpected key permissions')
            if newforks[guest][0]!={'bytes':host.stat().st_size,'sha256':S.digest(host)} or newforks[guest][1]['bytes']:
                raise ValueError('unexpected key fork')
        if set(after)-set(before)!=addedkeys:raise ValueError('unexpected new catalog records')
        for key,raw in before.items():
            new=after[key]
            if key==paths[PARENT][0]:
                at=paths[PARENT][1]+4
                if H.u32(new,at)!=H.u32(raw,at)+2 or new[:at]+new[at+4:]!=raw[:at]+raw[at+4:]:raise ValueError('unexpected parent change')
            else:
                changed=next((p for p in replacements if paths[p][0]==key),None)
                if changed:
                    at=paths[changed][1]+88
                    if H.u64(new,at)!=len(replacements[changed]) or new[:at]+new[at+8:]!=raw[:at]+raw[at+8:]:raise ValueError('unexpected configuration metadata')
                elif new!=raw:raise ValueError('original catalog changed')
        for path,pair in forks.items():
            expected=({'bytes':len(replacements[path]),'sha256':sha(replacements[path])},pair[1]) if path in replacements else pair
            if list(newforks[path])!=list(expected):raise ValueError('original fork changed: '+path)
        if S.digest(source)!=expected_source:raise ValueError('source changed during installation')
        report={'passed':True,'sourceSHA256':expected_source,'sourceUnchanged':True,'outputSHA256':S.digest(temporary),
                'modifiedPaths':sorted(replacements),'addedPaths':[PRIVATE,PUBLIC],
                'unchangedExistingFileForks':2*len(forks)-3,'newCatalogRecords':len(addedkeys),'preflight':proof,
                'configurationHashes':{p:sha(b) for p,b in replacements.items()},
                'keyHashes':{guest:S.digest(host) for guest,host,_ in additions},
                'toolHashes':{str(p):S.digest(p) for p in [Path(__file__),Path(S.__file__),Path(W.__file__)]}}
        with temporary.open('rb') as f:os.fsync(f.fileno())
        os.link(temporary,target)
        with receipt.open('x') as f:json.dump(report,f,indent=2);f.write('\n')
        return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--source-sha256',required=True)
    a=p.parse_args();print(json.dumps(install(a.source,a.output,a.source_sha256),indent=2))
