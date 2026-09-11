"""Local probe parser/HTTP contracts; never authenticates against a real service."""
import base64
import ctypes
import unittest
import test_weather_http as transport


class Field(ctypes.Structure):
    _fields_ = [('data', ctypes.c_void_p), ('length', ctypes.c_uint)]


class Fields(ctypes.Structure):
    _fields_ = [(key, Field) for key in ('r1', 'r2', 'cert', 'sig', 'hr1', 'hmackr2')] + [('decoded_hr1', ctypes.c_ubyte * 20)]


def request(phase, body):
    return transport.request(body, ['Host: dm5.google.com',
        'Content-Type: application/x-www-form-urlencoded', f'Content-Length: {len(body)}'],
        f'/youtube/accounts/applelogin{phase}')


class Youtube(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        transport.Transport.setUpClass()
        cls.c = transport.Transport.c
        cls.c.own_youtube_auth_parse.argtypes = [ctypes.c_uint, ctypes.c_char_p,
            ctypes.c_uint, ctypes.POINTER(Fields)]
    @classmethod
    def tearDownClass(cls):
        transport.Transport.tearDownClass()
    frame = transport.Transport.frame
    response = transport.Transport.response

    def parse(self, phase, body):
        result = Fields()
        status = self.c.own_youtube_auth_parse(phase, body, len(body), ctypes.byref(result))
        fields = {key: ctypes.string_at(getattr(result, key).data, getattr(result, key).length)
                  for key in ('r1', 'r2', 'cert', 'sig', 'hr1', 'hmackr2')}
        return status, fields, bytes(result.decoded_hr1)

    def test_hash_and_challenge(self):
        # Exercise all alphabet values and canonical unused bits against Python.
        for offset in range(256):
            digest = bytes((offset+i) & 255 for i in range(20))
            encoded = base64.urlsafe_b64encode(digest)
            for value in (encoded, encoded[:-1], encoded[:-1]+b'.', encoded[:-1]+b'%3D'):
                body = b'hr1='+value
                status, fields, decoded = self.parse(1, body)
                self.assertEqual((status, decoded), (1, digest))
                self.assertEqual(fields['hr1'], value)
        body = b'hr1='+base64.urlsafe_b64encode(bytes(range(20)))
        wire = request(1, body)
        for end in range(len(wire)):
            self.assertEqual(self.frame(wire[:end]), (0, 0, 0))
        ok, response = self.response(wire)
        self.assertEqual(ok, 1)
        header, payload = response.split(b'\r\n\r\n')
        self.assertIn(b'200 OK', header)
        pairs = dict(line.split(b'=', 1) for line in payload.splitlines())
        self.assertEqual(base64.urlsafe_b64decode(pairs[b'r2']), bytes(range(20)))
        self.assertEqual(pairs[b'hmackr2'], b'iphone1-local-auth-probe')
        self.assertNotIn(b'Auth=', payload)
        self.assertIn(b'Content-Length: '+str(len(payload)).encode(), header)

    def test_bad_phase1_never_gets_challenge(self):
        good = b'hr1=AAECAwQFBgcICQoLDA0ODxAREhM='
        for body in (b'', b'hr1=', good+b'&', good+b'&'+good, good+b'&extra=x',
                     good.replace(b'hr1', b'r1'), good.replace(b'M=', b'N='),
                     good.replace(b'=', b'==', 1), good[:-2]+b'=', good+b'=',
                     good.replace(b'A', b'+', 1), good.replace(b'A', b'/', 1),
                     good.replace(b'A', b'%', 1), good+b'\x00', b'x'*16385):
            self.assertEqual(self.parse(1, body)[0], 0, body)
            if body and len(body)<=16384:
                self.assertIn(b'400 Bad Request', self.response(request(1, body))[1])
        for args in ((1, None, 10, ctypes.byref(Fields())), (1, good, len(good), None)):
            self.assertEqual(self.c.own_youtube_auth_parse(*args), 0)

    def test_signed_request_is_observable_but_no_token(self):
        body = (b'r1=AAECAwQFBgcICQoLDA0ODxAREhM=&r2=AAECAwQFBgcICQoLDA0ODxAREhM='
                b'&cert=c3ludGhldGlj&sig=c3ludGhldGlj&hr1=AAECAwQFBgcICQoLDA0ODxAREhM='
                b'&hmackr2=iphone1-local-auth-probe')
        status, fields, _ = self.parse(2, body)
        self.assertEqual(status, 1)
        self.assertEqual(fields['sig'], b'c3ludGhldGlj')
        self.assertEqual(fields['cert'], b'c3ludGhldGlj')
        # Parsing asserts field structure, not cryptographic authenticity.
        for payload in (body, b'malformed', body+b'&sig=duplicate'):
            wire = request(2, payload)
            for end in range(len(wire)):
                self.assertEqual(self.frame(wire[:end]), (0, 0, 0))
            ok, response = self.response(wire)
            self.assertEqual(ok, 1)
            header, result = response.split(b'\r\n\r\n')
            self.assertIn(b'503 Service Unavailable', header)
            self.assertIn(b'LocalDiagnosticOnly', result)
            self.assertNotIn(b'Auth=', result)
            self.assertIn(b'Content-Length: '+str(len(result)).encode(), header)
        for bad in (body+b'&sig=x', body.replace(b'&sig=c3ludGhldGlj', b''), body+b'&other=x'):
            self.assertEqual(self.parse(2, bad)[0], 0)

    def test_cross_route_and_framing_rejected(self):
        good = request(1, b'hr1=AAECAwQFBgcICQoLDA0ODxAREhM=')
        for bad in (good+b'x', good+good, good.replace(b'POST ', b'GET '),
                    good.replace(b'dm5.google.com', b'iphone-wu.apple.com'),
                    good.replace(b'dm5.google.com', b'127.0.0.1'),
                    good.replace(b'application/x-www-form-urlencoded', b'text/xml'),
                    good.replace(b'applelogin1 ', b'applelogin1?extra=1 '),
                    good.replace(b'applelogin1 ', b'applelogin3 '),
                    good.replace(b'/youtube/accounts/applelogin1', b'/dgw'),
                    good.replace(b'\r\n\r\n', b'\r\nTransfer-Encoding: chunked\r\n\r\n'),
                    good.replace(b'\r\n\r\n', b'\r\nContent-Length: 32\r\n\r\n'),
                    good.replace(b'\r\n\r\n', b'\r\nHost: dm5.google.com\r\n\r\n')):
            self.assertEqual(self.frame(bad), (-1, 0, 0), bad)
        self.assertEqual(self.frame(good.replace(b'dm5.google.com', b'DM5.GOOGLE.COM:80'))[0], 1)
        # Finite output capacity failure never claims a successful response.
        self.assertEqual(self.response(good, 255), (0, b''))
        self.assertEqual(self.response(good, 256), (0, b''))


if __name__ == '__main__':
    unittest.main(verbosity=2)
