"""Actual C parser/body checks for statically established OS1 quote protocol."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest
from xml.etree import ElementTree as ET
HERE=Path(__file__).resolve().parent

def request(symbols,query_id='47',kind='getquotes'):
    root=ET.Element('request',devtype='Apple iPhone v1.0',deployver='Apple iPhone v1.0',app='YGoiPhoneClient',appver='1.0.0',api='finance',apiver='1.0.0',acknotification='0000')
    query=ET.SubElement(root,'query',id=query_id,timestamp='0',type=kind)
    items=ET.SubElement(query,'list')
    for symbol in symbols:ET.SubElement(items,'symbol').text=symbol
    return ET.tostring(root,encoding='utf-8',xml_declaration=True,short_empty_elements=False)

class StocksTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();library=Path(cls.tmp.name)/'stocks.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',str(HERE/'stocks.c'),'-o',str(library)],check=True)
        cls.lib=ctypes.CDLL(str(library));cls.fn=cls.lib.own_stocks_request
        cls.fn.argtypes=[ctypes.c_char_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint)]
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def response(self,body,cap=4096):
        out=ctypes.create_string_buffer(4096);n=ctypes.c_uint(99)
        if not self.fn(body,len(body),out,cap,ctypes.byref(n)):
            self.assertEqual(n.value,0);return None
        return out.raw[:n.value]
    def test_all_default_symbols_exact_quote_fields(self):
        symbols=['^DJI','AAPL','GOOG','YHOO','T'];root=ET.fromstring(self.response(request(symbols)))
        self.assertEqual(root.get('demo'),'true');self.assertEqual(root.find('result').get('id'),'47')
        quotes=root.findall('./result/list/quote');self.assertEqual([q.findtext('symbol') for q in quotes],symbols)
        for q in quotes:
            self.assertEqual([x.tag for x in q],['symbol','price','change','changepercent','status','link'])
            for name in ('price','change','changepercent'):float(q.findtext(name))
            self.assertEqual(q.findtext('status'),'0');self.assertEqual(q.findtext('link'),'http://127.0.0.1/')
        self.assertEqual(quotes[1].findtext('price'),'120.00');self.assertEqual(quotes[2].findtext('change'),'-2.50')
    def test_unknown_dedup_order_and_escaped_symbols(self):
        root=ET.fromstring(self.response(request(['T','unknown<&','AAPL','T'])))
        self.assertEqual([q.findtext('symbol') for q in root.findall('./result/list/quote')],['T','AAPL'])
        self.assertEqual(len(ET.fromstring(self.response(request(['unknown']))).findall('./result/list/quote')),0)
    def test_dynamic_id_and_numeric_entities(self):
        for ident in ('0','2147483647','00047'):
            body=request(['AAPL'],ident).replace(b'AAPL',b'&#65;APL')
            self.assertEqual(ET.fromstring(self.response(body)).find('result').get('id'),ident)
    def test_unsupported_malformed_and_limits(self):
        bodies=[request(['AAPL'],kind='getchart'),request(['AAPL'],kind='getsymbol'),request([]),request(['AAPL']*17),request(['x'*129]),request(['AAPL'],'2147483648'),request(['AAPL'],'-1'),request(['AAPL'],'x'),request(['AAPL']).replace(b'api="finance"',b'api="weather"'),request(['AAPL']).replace(b'</list>',b'</wrong>'),request(['AAPL']).replace(b'AAPL',b'&broken;'),request(['AAPL']).replace(b'timestamp="0"',b'id="48"'),request(['AAPL'])+b'<request/>',b'<!DOCTYPE request>'+request(['AAPL']),b'x'*16385]
        for body in bodies:
            with self.subTest(body=body[:70]):self.assertIsNone(self.response(body))
    def test_all_truncated_prefixes_and_output_bound(self):
        body=request(['AAPL'])
        for cut in range(len(body)):self.assertIsNone(self.response(body[:cut]))
        response=self.response(body);self.assertEqual(self.response(body,len(response)),response);self.assertIsNone(self.response(body,len(response)-1))

if __name__=='__main__':unittest.main()
