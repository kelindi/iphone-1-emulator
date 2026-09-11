"""Real C framing/builder contracts, without a guest or historical endpoint."""
import ctypes
import pathlib
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent


def request(body, headers=None, target='/dgw?imei=demo&apptype=weather'):
    headers = headers if headers is not None else [
        'Host: iphone-wu.apple.com', 'Content-Type: text/xml',
        f'Content-Length: {len(body)}']
    return (f'POST {target} HTTP/1.1\r\n' + '\r\n'.join(headers) + '\r\n\r\n').encode() + body


class Transport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        lib = pathlib.Path(cls.temp.name) / 'weather.so'
        subprocess.run(['cc', '-std=c11', '-O2', '-shared', '-fPIC',
                        *map(str, [ROOT / n for n in ('weather_http.c', 'weather_request.c', 'weather.c', 'weather_support.c', 'http.c', 'stocks.c', 'youtube_auth.c', 'youtube_catalog.c')]),
                        '-o', str(lib)], check=True, timeout=30)
        cls.c = ctypes.CDLL(str(lib))
        cls.c.own_weather_http_frame.argtypes = [ctypes.c_char_p, ctypes.c_uint,
                                                ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
        cls.c.own_weather_http_response.argtypes = [ctypes.c_char_p, ctypes.c_uint,
                                                  ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def frame(self, data):
        offset, size = ctypes.c_uint(99), ctypes.c_uint(99)
        code = self.c.own_weather_http_frame(data, len(data), ctypes.byref(offset), ctypes.byref(size))
        return code, offset.value, size.value

    def response(self, wire, capacity=32768):
        output = ctypes.create_string_buffer(capacity)
        size = ctypes.c_uint(99)
        status = self.c.own_weather_http_response(wire, len(wire), output, capacity, ctypes.byref(size))
        return status, output.raw[:size.value]

    def test_full_bodies_and_every_fragment_boundary(self):
        for name in ('forecast-request.xml', 'search-request.xml'):
            body = (ROOT.parent / 'fixtures' / name).read_bytes()
            wire = request(body)
            for end in range(len(wire)):
                self.assertEqual(self.frame(wire[:end]), (0, 0, 0), (name, end))
            self.assertEqual(self.frame(wire), (1, len(wire) - len(body), len(body)))
            ok, result = self.response(wire)
            self.assertEqual(ok, 1)
            headers, xml = result.split(b'\r\n\r\n', 1)
            self.assertIn(b'200 OK', headers)
            length = next(row.split(b':', 1)[1] for row in headers.split(b'\r\n') if row.startswith(b'Content-Length:'))
            self.assertEqual(int(length), len(xml))
            # Independent existing Python service, compared structurally.
            import sys
            sys.path.insert(0, str(ROOT.parents[2]))
            from tools.legacy_services.weather import respond
            self.assertEqual(ET.tostring(ET.fromstring(xml)), ET.tostring(ET.fromstring(respond(body))))

    def test_framing_ambiguity_and_wrong_route(self):
        body = b'<bad></bad>'
        good = request(body)
        bad = [good + b'x', good + good, good.replace(b'POST ', b'GET ', 1),
               good.replace(b'\r\n', b'\n'), good.replace(b'Host:', b' Host:', 1),
               good.replace(b'Host:', b'Ho\x00st:', 1),
               good.replace(b'Content-Length:', b'Content-Length: -', 1)]
        for extra in ('Content-Length: 11', 'Transfer-Encoding: chunked', 'Expect: 100-continue',
                      'Host: iphone-wu.apple.com', 'Content-Type: text/xml'):
            bad.append(good.replace(b'\r\n\r\n', b'\r\n' + extra.encode() + b'\r\n\r\n', 1))
        for value in ('0', '16385', '999999999999999999999999999999', '+11', '11,11', ''):
            bad.append(request(body, ['Host: iphone-wu.apple.com', 'Content-Type: text/xml', 'Content-Length: ' + value]))
        for target in ('http://iphone-wu.apple.com/dgw', '/dgw?apptype=unknown', '/other',
                       '/dgw?apptype=weather&apptype=weather', '/dgw?imei=x', '/dgw?apptype=weather&'):
            bad.append(request(body, target=target))
        for item in bad:
            self.assertEqual(self.frame(item), (-1, 0, 0), item[:150])
            self.assertEqual(self.response(item), (0, b''))

    def test_bounds_and_case(self):
        body = b'x' * 16384
        self.assertEqual(self.frame(request(body))[0], 1)
        self.assertEqual(self.frame(request(body + b'x'))[0], -1)
        self.assertEqual(self.frame(b'P' * 4096)[0], -1)
        self.assertEqual(self.frame(b'P' * 20481)[0], -1)
        wire = request(b'x', ['host: IPHONE-WU.APPLE.COM:80', 'content-type: TEXT/XML; CHARSET=UTF-8', 'content-length: 1'])
        self.assertEqual(self.frame(wire)[0], 1)
        self.assertEqual(self.response(wire)[1].split(b'\r\n')[0], b'HTTP/1.0 400 Bad Request')
        self.assertEqual(self.response(wire, 255), (0, b''))

    def test_actual_weather_time_parameter_and_fragments(self):
        # Own guest's exact captured header layout, with equal-length synthetic
        # IMEI; body bytes recovered independently from the same snapshot.
        header = (ROOT.parent/'fixtures/weather-original-t-header.http').read_bytes()
        body = (ROOT.parent/'fixtures/weather-original-t-body.xml').read_bytes()
        self.assertEqual(len(header), 299)
        self.assertEqual(len(body), 355)
        self.assertIn(b'Content-Length: 355\r\n', header)
        wire = header + body
        for end in range(len(wire)):
            self.assertEqual(self.frame(wire[:end]), (0, 0, 0), end)
        self.assertEqual(self.frame(wire), (1, 299, 355))
        ok, response = self.response(wire)
        self.assertEqual(ok, 1)
        self.assertIn(b'200 OK', response)
        self.assertIn(b'Cupertino', response)
        xml = ET.fromstring(response.split(b'\r\n\r\n', 1)[1])
        self.assertEqual(xml.find('result').attrib['id'], '30')
        items = xml.findall('result/list/item')
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0].find('location').attrib,
                         {'id': 'USCA0273|12797509', 'city': 'Cupertino'})
        self.assertEqual(items[0].find('condition').attrib,
                         {'time': '720', 'temp': '72', 'code': '32'})
        self.assertEqual([(day.attrib['high'], day.attrib['low'], day.attrib['code'])
                          for day in items[0].findall('forecast')],
                         [('74', '55', '32'), ('73', '54', '32'), ('75', '56', '32'),
                          ('76', '55', '32'), ('72', '53', '32'), ('71', '54', '32')])
        self.assertEqual(self.response(wire.replace(b'&t=3 ', b' ')), (ok, response))
        for value in ('0', '3', '4294967295', '0003'):
            self.assertEqual(self.frame(wire.replace(b'&t=3 ', f'&t={value} '.encode()))[0], 1)
        for value in ('', '-1', '+1', '1.0', '3x', '%33', '4294967296', '99999999999999999999', '3&t=3'):
            self.assertEqual(self.frame(wire.replace(b'&t=3 ', f'&t={value} '.encode()))[0], -1)
        self.assertEqual(self.frame(wire.replace(b'&apptype=weather&t=3', b'&t=3&apptype=weather'))[0], 1)
        self.assertEqual(self.frame(wire.replace(b'apptype=weather', b'apptype=finance'))[0], -1)

    def test_freestanding_memset_exact_span(self):
        self.c.memset.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_size_t]
        self.c.memset.restype = ctypes.c_void_p
        for count in (0, 1, 31, 512):
            for value in (0, 255, 0x1234, -1):
                memory = ctypes.create_string_buffer(b'A' * 516)
                pointer = ctypes.addressof(memory) + 2
                self.assertEqual(self.c.memset(pointer, value, count), pointer)
                self.assertEqual(memory.raw[:516], b'AA' + bytes([value & 255]) * count + b'A' * (514-count))

    def test_safari_routes_delegate_identical_bytes(self):
        class Result(ctypes.Structure):
            _fields_ = [('length', ctypes.c_uint), ('status', ctypes.c_int), ('route', ctypes.c_char_p)]
        self.c.own_http_response.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_void_p,
                                             ctypes.c_uint, ctypes.POINTER(Result)]
        for target, marker in (('/', b'HOME-1'), ('/next', b'NEXT-1'),
                               ('/echo?message=hi', b'Message: hi'),
                               ('/echo?message=%3Cscript%3E', b'&lt;script&gt;'),
                               ('/echo?message=%GG', b'400 Bad Request')):
            wire = f'GET {target} HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n'.encode()
            for end in range(len(wire)):
                self.assertEqual(self.frame(wire[:end]), (0, 0, 0))
            self.assertEqual(self.frame(wire), (1, len(wire), 0))
            output, result = ctypes.create_string_buffer(32768), Result()
            self.assertEqual(self.c.own_http_response(wire, len(wire), output, 32768, ctypes.byref(result)), 1)
            self.assertEqual(self.response(wire), (1, output.raw[:result.length]))
            self.assertIn(marker, self.response(wire)[1])

    def test_get_cannot_enter_weather_or_accept_body(self):
        good = b'GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n'
        bad = [good+b'x', good+good, good.replace(b'GET / ', b'GET /dgw '),
               good.replace(b'GET / ', b'GET /unknown '), good.replace(b'GET / ', b'POST / '),
               good.replace(b'127.0.0.1', b'iphone-wu.apple.com'),
               good.replace(b'\r\n\r\n', b'\r\nContent-Length: 1\r\n\r\nx'),
               good.replace(b'\r\n\r\n', b'\r\nTransfer-Encoding: chunked\r\n\r\n'),
               good.replace(b'\r\n\r\n', b'\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n'),
               good.replace(b'\r\n\r\n', b'\r\nX-Padding: '+b'a'*2048+b'\r\n\r\n')]
        for wire in bad:
            self.assertEqual(self.frame(wire), (-1, 0, 0))
            self.assertEqual(self.response(wire), (0, b''))
        zero = good.replace(b'\r\n\r\n', b'\r\nContent-Length: 0\r\n\r\n')
        self.assertEqual(self.frame(zero), (1, len(zero), 0))

    def test_finance_quotes_and_service_separation(self):
        body = b'<request api="finance" apiver="1.0.0"><query id="123" type="getquotes"><list><symbol>^DJI</symbol><symbol>AAPL</symbol><symbol>GOOG</symbol><symbol>YHOO</symbol><symbol>T</symbol></list></query></request>'
        wire = request(body, target='/dgw?imei=demo&apptype=finance')
        for end in range(len(wire)):
            self.assertEqual(self.frame(wire[:end]), (0, 0, 0))
        self.assertEqual(self.frame(wire), (1, len(wire)-len(body), len(body)))
        ok, response = self.response(wire)
        self.assertEqual(ok, 1)
        headers, xml = response.split(b'\r\n\r\n', 1)
        self.assertIn(b'200 OK', headers)
        self.assertIn(b'X-iPhone1-Demo: fixed-stocks', headers)
        tree = ET.fromstring(xml)
        self.assertEqual(tree.find('result').attrib['id'], '123')
        self.assertEqual([(q.findtext('symbol'), q.findtext('price')) for q in tree.findall('result/list/quote')],
                         [('^DJI', '13500.00'), ('AAPL', '120.00'), ('GOOG', '500.00'), ('YHOO', '25.00'), ('T', '40.00')])
        weather = (ROOT.parent/'fixtures/forecast-request.xml').read_bytes()
        for payload, route in ((body, '/dgw?apptype=weather'), (body, '/dgw'),
                               (weather, '/dgw?apptype=finance'),
                               (body.replace(b'getquotes', b'getchart'), '/dgw?apptype=finance')):
            result = self.response(request(payload, target=route))
            self.assertEqual(result[0], 1)
            self.assertIn(b'400 Bad Request', result[1])
            self.assertTrue(result[1].endswith(b'Content-Length: 0\r\n\r\n'))
        self.assertEqual(self.frame(request(body, target='/dgw?apptype=finance&apptype=weather'))[0], -1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
