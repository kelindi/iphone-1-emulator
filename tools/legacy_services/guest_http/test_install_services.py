"""Synthetic HFSX audit: own service additions plus one exact hosts append."""
import contextlib
import hashlib
import io
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from unittest import mock
import install_services as S
import test_install as F

def fixture():
    image=F.fixture(); bs=F.F.BS
    node=image[6*bs:7*bs]; rows=[]
    for i in range(S.H.u16(node,10)):
        start=S.H.u16(node,bs-2*(i+1));end=S.H.u16(node,bs-2*(i+2));rows.append(bytes(node[start:end]))
    rows.append(F.record(5,'etc',1,12))
    hosts=bytearray(F.record(12,'hosts',2,13));offset=len(hosts)-248
    F.F.fork(hosts,offset+88,20,[(21,1)])
    rows.append(bytes(hosts));rows.sort(key=lambda row: (S.H.u32(row, 2), row[8:2 + S.H.u16(row, 0)]))
    new=bytearray(bs);new[8]=255;struct.pack_into('>H',new,10,len(rows));at=14
    for i,row in enumerate(rows):
        struct.pack_into('>H',new,bs-2*(i+1),at);new[at:at+len(row)]=row;at+=len(row)
    struct.pack_into('>H',new,bs-2*(len(rows)+1),at)
    image[6*bs:7*bs]=new;struct.pack_into('>I',image,5*bs+20,len(rows))
    image[21*bs:21*bs+20]=b'127.0.0.1 localhost\n'
    # Block21 must be allocated; fixture bitmap is at block3.
    image[3*bs+(21>>3)]|=0x80>>(21&7)
    return image

class ServicesInstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.source=self.root/'source.img';self.source.write_bytes(fixture())
        self.program=self.root/'program';self.program.write_bytes(b'OWN WEATHER' * 100)
        self.plist=self.root/'service.plist';self.plist.write_bytes(plistlib.dumps({'Label':'com.iphone1.weather-http','ProgramArguments':[S.PROGRAM_PATH],'RunAtLoad':True}))
        self.target=self.root/'copy.img';self.old=b'127.0.0.1 localhost\n';self.hash=hashlib.sha256(self.old).hexdigest()
    def tearDown(self):self.tmp.cleanup()
    def install(self,**kw):
        args=dict(source=self.source,target=self.target,expected_source=S.digest(self.source),program=self.program,expected_program=S.digest(self.program),plist=self.plist,expected_plist=S.digest(self.plist),expected_hosts=self.hash);args.update(kw)
        with contextlib.redirect_stdout(io.StringIO()):return S.install(**args)
    def test_copy_exact_hosts_and_all_other_old_forks(self):
        original=self.source.read_bytes();before=S.inventory(self.source)[2];r=self.install()
        self.assertTrue(r['passed']);self.assertEqual(self.source.read_bytes(),original)
        after=S.inventory(self.target)[2]
        for path,forks in before.items():
            if path!=S.HOSTS_PATH:self.assertEqual(forks,after[path])
        work=S.W.Work(self.target)
        try:self.assertEqual(work.read_file_path(S.HOSTS_PATH),S.routed_hosts(self.old,self.hash))
        finally:work.vol.f.close()
        self.assertEqual(r['unchangedExistingFileForks'],len(before)*2-1)
        self.assertEqual(set(after)-set(before),{S.PROGRAM_PATH,S.PLIST_PATH})
    def test_unrelated_fork_mutation_fails_audit_before_publication(self):
        original=self.source.read_bytes();replace=S.W.Work.replace_file_path
        def corrupt(work,path,data):
            replace(work,path,data)
            replace(work,'/private/var/root/Library/Preferences/.GlobalPreferences.plist',b'EVIL')
        with mock.patch.object(S.W.Work,'replace_file_path',corrupt):
            with self.assertRaisesRegex(ValueError,'original file fork changed'):self.install()
        self.assertFalse(self.target.exists());self.assertEqual(self.source.read_bytes(),original)
    def test_insufficient_allocation_preflight(self):
        self.program.write_bytes(b'X'*(len(self.source.read_bytes())*2))
        with self.assertRaisesRegex(ValueError,'contiguous free blocks'):self.install()
        self.assertFalse(self.target.exists())
    def test_wrong_hosts_hash_no_publication(self):
        original=self.source.read_bytes()
        with self.assertRaisesRegex(ValueError,'hosts hash'):self.install(expected_hosts='0'*64)
        self.assertFalse(self.target.exists());self.assertEqual(self.source.read_bytes(),original)
    def test_existing_target_no_overwrite(self):
        self.target.write_bytes(b'KEEP')
        with self.assertRaises(ValueError):self.install()
        self.assertEqual(self.target.read_bytes(),b'KEEP')
    def test_reject_conflicting_route_preserve_comments(self):
        old=b'# iphone-wu.apple.com\n127.0.0.1 localhost'
        result=S.routed_hosts(old,hashlib.sha256(old).hexdigest());self.assertTrue(result.startswith(old+b'\n'))
        for old in (b'1.2.3.4 IPHONE-WU.APPLE.COM.\n',b'127.0.0.1 other iphone-wu.apple.com\n'):
            with self.assertRaisesRegex(ValueError,'already exists'):S.routed_hosts(old,hashlib.sha256(old).hexdigest())
    def test_geometry_failure_before_copy(self):
        with mock.patch.object(S,'preflight',side_effect=ValueError('catalog leaf has insufficient space')):
            with self.assertRaises(ValueError):self.install()
        self.assertFalse(self.target.exists());self.assertEqual(list(self.root.glob('*.partial')),[])


class ResolverInstallTests(ServicesInstallTests):
    # Use the synthetic fixture's existing hosts fork for the routing resource;
    # the production absolute path is separately asserted below.
    def resolver_install(self, xml=None, **kw):
        if xml is None:
            xml = plistlib.dumps([{"Options":"mdns", "SupplementalMatchDomains":["local"]}])
        work = S.W.Work(self.source)
        try: work.replace_file_path(S.HOSTS_PATH, xml)
        finally: work.vol.f.close()
        with mock.patch.object(S, 'RESOLVERS_PATH', S.HOSTS_PATH):
            return self.install(expected_hosts=None, expected_resolvers=hashlib.sha256(xml).hexdigest(), **kw)
    def test_resolver_copy_bytes_semantics_and_unrelated_forks(self):
        original = plistlib.dumps([{"Options":"mdns", "SupplementalMatchDomains":["local"]}])
        report = self.resolver_install(original)
        self.assertNotIn('hostsPath',report)
        self.assertEqual(bytes.fromhex(report['resolversAfterHex']), S.routed_resolvers(original,hashlib.sha256(original).hexdigest()))
        self.assertEqual(plistlib.loads(bytes.fromhex(report['resolversAfterHex'])), plistlib.loads(original)+[S.RESOLVER_ENTRY])
        before=S.inventory(self.source)[2];after=S.inventory(self.target)[2]
        for path,forks in before.items():
            if path!=S.HOSTS_PATH:self.assertEqual(forks,after[path])
        self.assertEqual(S.RESOLVERS_PATH,'/System/Library/SystemConfiguration/IPMonitor.bundle/Resolvers.plist')
    def test_explicit_youtube_route_preserves_existing_resource(self):
        old=plistlib.dumps([{"Options":"mdns","SupplementalMatchDomains":["local"]}])
        report=self.resolver_install(old,youtube_auth=True)
        new=bytes.fromhex(report['resolversAfterHex'])
        self.assertTrue(report['youtubeAuth'])
        self.assertEqual(plistlib.loads(new),plistlib.loads(old)+[{"ServerAddresses":["127.0.0.1"],"SupplementalMatchDomains":["iphone-wu.apple.com","dm5.google.com"]}])
        self.assertEqual(report['unchangedExistingFileForks'],2*len(S.inventory(self.source)[2])-1)
    def test_youtube_route_requires_resolvers_and_rejects_conflicts(self):
        with self.assertRaisesRegex(ValueError,'requires resolver'):self.install(youtube_auth=True)
        self.assertFalse(self.target.exists())
        old=plistlib.dumps([{"SupplementalMatchDomains":["DM5.GOOGLE.COM."]}])
        with self.assertRaisesRegex(ValueError,'already exists'):self.resolver_install(old,youtube_auth=True)
        self.assertFalse(self.target.exists())

    def test_resolver_capacity_rejected_without_publication(self):
        xml=plistlib.dumps([{'Options':'x'*3700}])
        with self.assertRaisesRegex(ValueError,'append exceeds'):self.resolver_install(xml)
        self.assertFalse(self.target.exists())
    def test_resolver_wrong_hash_and_duplicate_no_publication(self):
        xml=plistlib.dumps([S.RESOLVER_ENTRY])
        original=self.source.read_bytes()
        with mock.patch.object(S,'RESOLVERS_PATH',S.HOSTS_PATH):
            with self.assertRaisesRegex(ValueError,'resolvers hash mismatch'):
                self.install(expected_hosts=None,expected_resolvers='0'*64)
        self.assertEqual(self.source.read_bytes(),original)
        self.assertFalse(self.target.exists())
        with self.assertRaisesRegex(ValueError,'already exists'):self.resolver_install(xml)
        self.assertFalse(self.target.exists())
    def test_exclusive_api_mode(self):
        with self.assertRaisesRegex(ValueError,'exactly one'):self.install(expected_resolvers='a'*64)
        with self.assertRaisesRegex(ValueError,'exactly one'):self.install(expected_hosts=None)
    def test_resolver_parser_rejections(self):
        forms=[b'garbage',plistlib.dumps({}),plistlib.dumps(['x']),
               plistlib.dumps([{'SupplementalMatchDomains':['IPHONE-WU.APPLE.COM.']}]),
               b'<plist><array><dict><key>A</key><string>x</string><key>A</key><string>y</string></dict></array></plist>',
               b'<plist><array><dict></array></plist>',
               plistlib.dumps([{'SupplementalMatchDomains':'local'}])]
        for data in forms:
            with self.subTest(data=data):
                with self.assertRaises(ValueError):S.routed_resolvers(data,hashlib.sha256(data).hexdigest())
        with self.assertRaisesRegex(ValueError,'hash mismatch'):S.routed_resolvers(plistlib.dumps([]),'0'*64)
    def test_preserves_comments_and_exact_old_bytes(self):
        xml=b'<?xml version="1.0"?><plist version="1.0"><array><!-- keep this --><dict><key>Options</key><string>mdns</string></dict>\n</array></plist>\n'
        new=S.routed_resolvers(xml,hashlib.sha256(xml).hexdigest())
        self.assertEqual(new.replace(S.RESOLVER_INSERT,b'',1),xml)
        self.assertEqual(plistlib.loads(new)[:-1],plistlib.loads(xml))

if __name__=='__main__':unittest.main()
