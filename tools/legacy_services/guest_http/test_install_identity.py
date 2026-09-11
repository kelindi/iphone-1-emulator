import contextlib
import io
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from unittest import mock
from cryptography import x509
from cryptography.hazmat.primitives import serialization
import install_identity as I
import test_install as F

def fixture():
    image=F.fixture();bs=F.F.BS;node=image[6*bs:7*bs];rows=[]
    for i in range(I.H.u16(node,10)):
        a=I.H.u16(node,bs-2*(i+1));b=I.H.u16(node,bs-2*(i+2));rows.append(bytes(node[a:b]))
    rows.extend([F.record(8,'Lockdown',1,12),F.record(10,'Lockdown',1,13)])
    for parent,name,ident,block,data in [(12,'activation_record.plist',14,21,plistlib.dumps({'Keep':'unchanged'})),(13,'iPhoneActivation.pem',15,22,b'OLD'),(13,'iPhoneDeviceCA.pem',16,23,b'OLD')]:
        row=bytearray(F.record(parent,name,2,ident));F.F.fork(row,len(row)-248+88,len(data),[(block,1)]);rows.append(bytes(row))
        image[block*bs:block*bs+len(data)]=data;image[3*bs+(block>>3)]|=0x80>>(block&7)
    rows.sort(key=lambda r:(I.H.u32(r,2),r[8:2+I.H.u16(r,0)]));node=bytearray(bs);node[8]=255;struct.pack_into('>H',node,10,len(rows));at=14
    for i,row in enumerate(rows):struct.pack_into('>H',node,bs-2*(i+1),at);node[at:at+len(row)]=row;at+=len(row)
    struct.pack_into('>H',node,bs-2*(len(rows)+1),at);image[6*bs:7*bs]=node;struct.pack_into('>I',image,5*bs+20,len(rows));return image

class IdentityInstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.source=self.root/'source.img';self.source.write_bytes(fixture());self.target=self.root/'output.img'
    def tearDown(self):self.tmp.cleanup()
    def install(self):
        with contextlib.redirect_stdout(io.StringIO()):return I.install(self.source,self.target,I.S.digest(self.source))
    def test_copy_identity_matches_and_all_other_forks_preserved(self):
        old=self.source.read_bytes();before=I.S.inventory(self.source)[2];r=self.install();self.assertTrue(r['passed']);self.assertEqual(old,self.source.read_bytes())
        w=I.W.Work(self.target)
        try:
            private=serialization.load_pem_private_key(w.read_file_path(I.PRIVATE),None)
            public=serialization.load_pem_public_key(w.read_file_path(I.PUBLIC))
            record=plistlib.loads(w.read_file_path(I.RECORD));cert=x509.load_pem_x509_certificate(record['DeviceCertificate'])
            self.assertEqual(private.public_key().public_numbers(),public.public_numbers());self.assertEqual(public.public_numbers(),cert.public_key().public_numbers());self.assertEqual(record['Keep'],'unchanged')
        finally:w.vol.f.close()
        after=I.S.inventory(self.target)[2]
        for path,pair in before.items():
            if path not in [I.RECORD,I.ACTIVATION,I.CA]:self.assertEqual(pair,after[path])
        self.assertNotIn('PRIVATE KEY',str(r));self.assertEqual(list(self.root.glob('identity-*')),[])
    def test_opt_in_catalog_ids_match_preflight_and_preserve_next_id(self):
        r=self.install()
        w=I.W.Work(self.source)
        try:before_next=w.next_catalog_id
        finally:w.vol.f.close()
        w=I.W.Work(self.target)
        try:self.assertEqual(w.next_catalog_id,before_next)
        finally:w.vol.f.close()
        after=I.S.inventory(self.target)[1]
        for guest in [I.PRIVATE,I.PUBLIC]:
            actual=I.H.u32(after[guest][2],8)
            expected={row['cnid'] for row in r['preflight']['catalog'] if row['path']==guest}
            self.assertEqual(expected,{actual});self.assertLess(actual,before_next)
    def test_default_file_allocator_remains_sequential(self):
        w=I.W.Work(self.source)
        try:
            expected=w.next_catalog_id
            with contextlib.redirect_stdout(io.StringIO()):w.add_file_data(I.PRIVATE,b'own test',mode=0o600,uid=0,gid=0)
            self.assertEqual(w.next_catalog_id,expected+1)
        finally:w.vol.f.close()
        self.assertEqual(I.H.u32(I.S.inventory(self.source)[1][I.PRIVATE][2],8),expected)
    def test_capacity_rejects_before_copy(self):
        original=I.W._local_activation_data
        def oversized(**kw):
            a,c,r,k=original(**kw);return b'X'*5000,c,r,k
        with mock.patch.object(I.W,'_local_activation_data',oversized):
            with self.assertRaisesRegex(ValueError,'capacity'):self.install()
        self.assertFalse(self.target.exists())
    def test_allocation_preflight_failure(self):
        with mock.patch.object(I.S,'preflight',side_effect=ValueError('insufficient contiguous free blocks')):
            with self.assertRaisesRegex(ValueError,'contiguous'):self.install()
        self.assertFalse(self.target.exists());self.assertEqual(list(self.root.glob('identity-*')),[])
    def test_unrelated_fork_mutation_rejected(self):
        replace=I.W.Work.replace_file_path
        def corrupt(w,path,data):
            replace(w,path,data);replace(w,'/private/var/root/Library/Preferences/.GlobalPreferences.plist',b'EVIL')
        with mock.patch.object(I.W.Work,'replace_file_path',corrupt):
            with self.assertRaisesRegex(ValueError,'original fork changed'):self.install()
        self.assertFalse(self.target.exists())
    def test_wrong_hash_and_existing_target(self):
        with self.assertRaisesRegex(ValueError,'hash mismatch'):I.install(self.source,self.target,'0'*64)
        self.target.write_bytes(b'KEEP')
        with self.assertRaisesRegex(ValueError,'new'):self.install()
        self.assertEqual(self.target.read_bytes(),b'KEEP')

if __name__=='__main__':unittest.main()
