"""Small synthetic audit fixtures. These are not guest traces or milestones."""
from pathlib import Path
import tempfile,struct,json,shutil,unittest
import audit
class AuditTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name)
  old=Path('.context/sdio-first/checkpoint-v2/full-attempt-1')
  for a,b in {'low.bin':'full-low.bin','high.bin':'full-high.bin','low-mask.bin':'full-low-mask.bin','high-mask.bin':'full-high-mask.bin','handoff.bin':'full-handoff-frame.bin','helper.bin':'full-received-helper.bin'}.items():shutil.copy2(old/b,self.p/a)
  self.reads=[(0x20,0x1009),(0x10,0x1000),(0x11,0x1000),(0x34,0x10dc),(0x35,0x10fe)]
  self.report={'passed':True,'normalOutsideRunStop':True,'timeout':False,'instructionLimit':100000000,'instructions':99,'totalInstructions':17517070116,'ticks':256764307,'loadSeconds':1,'runSeconds':1,'dataCompletions':504,'imageUnchanged':True,'admissionDeviceUnchanged':True,'statusReads':1,'mode':'positive','mainBoundary':True,'mainReturn':True,'verifyEntered':True,'pc':'c0380208','runtimePhase':2}
  self.gates={'armSVC':True,'cpsr':'00000113','orderedFEDCPair':True}
 def tearDown(self):self.tmp.cleanup()
 def save(self):
  data=bytearray()
  for n,(reg,value) in enumerate(self.reads):
   for a,v,w in [(0x38d0000c,0x10000000|(reg<<9),1),(0x38d00008,0x80050074,1),(0x38d00020,value,0)]:data.extend(struct.pack('<QQIIII',17517070018+n,256764209+n,0xc0431a00,a,v,256|8|w))
  (self.p/'transactions.bin').write_bytes(data);self.report['transactions']=len(data)//32
  (self.p/'report.json').write_text(json.dumps(self.report));(self.p/'gate-mode-pair.json').write_text(json.dumps(self.gates))
 def reject(self):
  self.save()
  with self.assertRaises(AssertionError):audit.audit(self.p)
 def test_positive_shape(self):self.save();self.assertTrue(audit.audit(self.p)['passed'])
 def test_thumb_alias(self):self.gates['cpsr']='00000133';self.reject()
 def test_irq_mode(self):self.gates['cpsr']='00000112';self.reject()
 def test_missing_ordered_pair(self):self.gates['orderedFEDCPair']=False;self.reject()
 def test_reversed_marker(self):self.reads[-2:]=list(reversed(self.reads[-2:]));self.reject()
 def test_marker_error(self):self.reads[-1]=(0x35,0x18fe);self.reject()
 def test_later_register6(self):self.reads.append((6,0x1000));self.reject()
 def test_zero_request_missing(self):del self.reads[1];self.reject()
 def test_zero_request_wrong(self):self.reads[1]=(0x10,0x1010);self.reject()
 def test_incomplete_host_image(self):self.report['mainBoundary']=False;self.reject()
 def test_changed_bank(self):b=bytearray((self.p/'low.bin').read_bytes());b[23]^=1;(self.p/'low.bin').write_bytes(b);self.reject()
 def test_changed_mask(self):b=bytearray((self.p/'high-mask.bin').read_bytes());b[-1]=1;(self.p/'high-mask.bin').write_bytes(b);self.reject()
 def test_negative_exact100pairs(self):
  self.report.update(mode='no-marker',pc='c0380224',r0=0);self.gates['orderedFEDCPair']=False
  self.reads=self.reads[:3]+[(0x34,0x1000),(0x35,0x1000)]*100
  self.report['instructions']=1000;self.report['totalInstructions']=17517071017;self.report['ticks']=256765208;self.save();self.assertTrue(audit.audit(self.p)['passed'])
  self.reads=self.reads[:-2];self.reject()
if __name__=='__main__':unittest.main()
