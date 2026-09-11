"""Real C wire framing/ranges; own media bytes, no guest playback claim."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parent
class CatalogTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.libs=[]
  for mode in [0,1]:
   out=Path(cls.tmp.name)/('demo%d.so'%mode)
   subprocess.run(['cc','-std=c11','-O2','-shared','-fPIC','-DOWN_YOUTUBE_DEMO='+str(mode),*[str(ROOT/n) for n in ['weather_http.c','youtube_catalog.c','youtube_auth.c','weather_request.c','weather.c','stocks.c','http.c','weather_support.c']],'-o',str(out)],check=True)
   lib=ctypes.CDLL(str(out));lib.own_weather_http_response.argtypes=[ctypes.c_char_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint)];cls.libs.append(lib)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def response(self,path,headers=b'',method=b'GET',host=b'127.0.0.1',body=b'',mode=1,cap=32768):
  wire=method+b' '+path+b' HTTP/1.1\r\nHost: '+host+b'\r\n'+headers+b'\r\n'+body
  buf=ctypes.create_string_buffer(32776);ctypes.memset(buf,0xa5,len(buf));size=ctypes.c_uint()
  ok=self.libs[mode].own_weather_http_response(wire,len(wire),buf,cap,ctypes.byref(size))
  self.assertEqual(buf.raw[cap:],b'\xa5'*(len(buf)-cap))
  if not ok:self.assertEqual(size.value,0);return None
  out=buf.raw[:size.value];h,b=out.split(b'\r\n\r\n',1);return h,b
 def test_auth_default_probe_and_demo(self):
  body=b'r1=x&r2=x&cert=x&sig=x&hr1=AAAAAAAAAAAAAAAAAAAAAAAAAAA=&hmackr2=x'
  headers=b'Content-Type: application/x-www-form-urlencoded\r\nContent-Length: '+str(len(body)).encode()+b'\r\n'
  for mode,status in [(0,b'503'),(1,b'200')]:
   h,b=self.response(b'/youtube/accounts/applelogin2',headers,b'POST',b'dm5.google.com',body,mode)
   self.assertIn(status,h.splitlines()[0]);self.assertEqual(b.startswith(b'Auth='),mode==1)
  body=b'bad=x';headers=b'Content-Type: application/x-www-form-urlencoded\r\nContent-Length: 5\r\n'
  h,b=self.response(b'/youtube/accounts/applelogin2',headers,b'POST',b'dm5.google.com',body)
  self.assertIn(b'400',h.splitlines()[0]);self.assertNotIn(b'Auth=',b)
 def test_original_featured_schema_and_host(self):
  path=b'/feeds/standardfeeds/recently_featured?start-index=1&max-results=25&format=2,3'
  auth=b'Authorization: GoogleLogin auth=iphone1-local-demo-token\r\n'
  h,b=self.response(path,auth,host=b'gdata.youtube.com');root=ET.fromstring(b)
  ns={'a':'http://www.w3.org/2005/Atom','m':'http://search.yahoo.com/mrss/'}
  entries=root.findall('a:entry',ns);self.assertEqual(len(entries),1);self.assertEqual(entries[0].find('a:id',ns).text,'iphone1-demo-1')
  refs=entries[0].findall('m:group/m:content',ns);self.assertEqual({r.attrib['format'] for r in refs},{'2','3'})
  self.assertIsNone(self.response(path,auth));self.assertIsNone(self.response(path,host=b'gdata.youtube.com'))
  self.assertIsNone(self.response(path,auth,host=b'gdata.youtube.com',mode=0))
  self.assertIsNone(self.response(path+b'&unknown=x',auth,host=b'gdata.youtube.com'))
 def test_ranges_against_actual_full_asset(self):
  path=b'/demo/clip-1.mp4';h,full=self.response(path);total=len(full)
  for spec,expected in [(b'0-0',full[:1]),(b'2-7',full[2:8]),(b'10-',full[10:]),(b'-9',full[-9:]),(b'0-999999',full)]:
   h,b=self.response(path,b'Range: bytes='+spec+b'\r\n');self.assertIn(b'206',h.splitlines()[0]);self.assertEqual(b,expected);self.assertIn(b'Content-Length: '+str(len(b)).encode(),h)
  for spec in [str(total).encode()+b'-',b'-0',b'7-2',b'0-1,4-5',b'42949672960-',b'one-two',b'1--2']:
   h,b=self.response(path,b'Range: bytes='+spec+b'\r\n');self.assertIn(b'416',h.splitlines()[0]);self.assertEqual(b,b'');self.assertIn(b'Content-Range: bytes */'+str(total).encode(),h)
  h,b=self.response(path,b'Range: bytes=0-3\r\n',method=b'HEAD');self.assertEqual(b,b'');self.assertIn(b'Content-Length: 4',h)
  h,b=self.response(path,b'Range: bytes=0-3\r\nRange: bytes=4-6\r\n');self.assertIn(b'416',h.splitlines()[0])
  self.assertIsNone(self.response(path,cap=256))
 def test_safari_unchanged_and_jpeg_head(self):
  self.assertEqual(self.response(b'/',mode=0),self.response(b'/',mode=1))
  h,b=self.response(b'/demo/clip-1.jpg');self.assertTrue(b.startswith(b'\xff\xd8'))
  hh,bb=self.response(b'/demo/clip-1.jpg',method=b'HEAD');self.assertEqual(bb,b'');self.assertIn(b'Content-Length: '+str(len(b)).encode(),hh)
if __name__=='__main__':unittest.main()
