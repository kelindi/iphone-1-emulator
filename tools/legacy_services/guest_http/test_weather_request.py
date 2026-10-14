"""Real C parser + existing original-shape fixtures, independent Python oracle."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest
from xml.etree import ElementTree as ET
import sys
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
import weather

class WeatherRequestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        library = Path(cls.tmp.name) / 'weather.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', str(HERE/'weather_request.c'), str(HERE/'weather.c'), '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.fn = cls.lib.own_weather_request
        cls.fn.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]
        cls.fn.restype = ctypes.c_int
        base = ROOT/'tools/legacy_services/fixtures/weather-demo-backend'
        cls.forecast = (base/'forecast-request.xml').read_bytes()
        cls.search = (base/'search-request.xml').read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def response(self, body, capacity=4096):
        output = ctypes.create_string_buffer(4096)
        length = ctypes.c_uint(999)
        status = self.fn(body, len(body), output, capacity, ctypes.byref(length))
        if not status:
            self.assertEqual(length.value, 0)
            return None
        return output.raw[:length.value]

    def test_original_fixtures_match_python_backend(self):
        for body in (self.forecast, self.search):
            self.assertEqual(ET.tostring(ET.fromstring(self.response(body))), ET.tostring(ET.fromstring(weather.respond(body))))

    def test_decoded_entities_and_case_whitespace(self):
        for phrase in (b'&#78;ew&#x20;York', b' CuPeRtInO ', b'unknown &amp; &lt; &gt; &quot; &apos;'):
            body = self.search.replace(b'<phrase>Cupertino</phrase>', b'<phrase>'+phrase+b'</phrase>')
            self.assertEqual(ET.tostring(ET.fromstring(self.response(body))), ET.tostring(ET.fromstring(weather.respond(body))))

    def test_reject_malformed_and_unsupported(self):
        cases = [b'', self.search+b'<request/>', self.search.replace(b'</query>', b'</wrong>'), self.search.replace(b'api="weather"', b'api="weather" api="weather"'), self.search.replace(b'type="getlocationid"', b'type="getlocationid"type="getlocationid"'), self.search.replace(b'<phrase>Cupertino</phrase>', b'<phrase><id>Cupertino</id></phrase>'), self.search.replace(b'Cupertino', b'&unknown;'), self.search.replace(b'Cupertino', b'&#0;'), self.search.replace(b'Cupertino', b'&#999999999999;'), self.search.replace(b'Cupertino', b']]>'), b'<!DOCTYPE request>'+self.search, self.search.replace(b'api="weather"', b'api="stocks"'), self.forecast.replace(b'<unit>f</unit>', b'<unit>c</unit>'), self.forecast.replace(b'<language>en</language>', b'<language>fr</language>'), self.forecast.replace(b'<list>', b'<list>'+b'<id>x</id>'*16), self.search.replace(b'Cupertino', b'x'*129), b'x'*16385]
        for body in cases:
            with self.subTest(body=body[:100]):
                self.assertIsNone(self.response(body))

    def test_all_truncations_reject(self):
        body = self.forecast.rstrip()
        for cut in range(len(body)):
            self.assertIsNone(self.response(body[:cut]))

    def test_exact_output_bound(self):
        body = self.response(self.forecast)
        self.assertEqual(self.response(self.forecast, len(body)), body)
        self.assertIsNone(self.response(self.forecast, len(body)-1))

if __name__ == '__main__':
    unittest.main()
