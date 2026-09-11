"""Offline proof fixtures use independently generated, test-owned RSA keys."""
import base64
from datetime import datetime, timedelta
import hashlib
import unittest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
import audit_youtube_auth as audit


def b64(data):
    return base64.urlsafe_b64encode(data).replace(b'=', b'.')


def http(first, body, request=False):
    return (first+b'\r\n'+(b'Host: dm5.google.com\r\nContent-Type: application/x-www-form-urlencoded\r\n' if request else b'Content-Type: text/plain\r\n')+b'Content-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)


def block(label, data):
    return label+b' %08x 00000000\n'%len(data)+b''.join(b'D '+data[i:i+32].hex().encode()+b'\n' for i in range(0, len(data), 32))


def exchange(serial, request, response):
    return (b'WEATHER_REQUEST_BEGIN %08x 00000005\n'%serial+block(b'WEATHER_REQUEST_BYTES', request)+
            block(b'WEATHER_RESPONSE_BYTES', response)+b'WEATHER_RESPONSE_SENT %08x %08x\n'%(serial, len(response)))


class Audit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = rsa.generate_private_key(public_exponent=65537, key_size=1024)
        cls.other = rsa.generate_private_key(public_exponent=65537, key_size=1024)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'owned offline probe test')])
        cls.cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(cls.key.public_key()).serial_number(1)
            .not_valid_before(datetime(2020, 1, 1)).not_valid_after(datetime(2020, 1, 1)+timedelta(days=365))
            .sign(cls.key, hashes.SHA256()).public_bytes(serialization.Encoding.PEM))

    def fixture(self, signing=None, changed_nonce=False, changed_signature=False, digest_info=False):
        key = signing or self.key
        r1, r2 = bytes(range(20, 40)), bytes(range(20))
        hr1 = hashlib.sha1(r1).digest()
        digest = hashlib.sha1(r2+r1).digest()
        if digest_info:
            digest = bytes.fromhex('3021300906052b0e03021a05000414')+digest
        nums = key.private_numbers()
        width = (nums.public_numbers.n.bit_length()+7)//8
        encoded = b'\0\1'+b'\xff'*(width-len(digest)-3)+b'\0'+digest
        signature = pow(int.from_bytes(encoded, 'big'), nums.d, nums.public_numbers.n).to_bytes(width, 'big')
        if changed_signature:
            signature = bytes([signature[0]^1])+signature[1:]
        if changed_nonce:
            r1 = b'x'+r1[1:]
        first = b'hr1='+b64(hr1).replace(b'.', b'%3D')
        challenge = b'r2='+b64(r2)+b'\nhmackr2=owned-local-challenge\n'
        second = b'&'.join([b'r1='+b64(r1), b'r2='+b64(r2), b'cert='+b64(self.cert.replace(b'-----END', b'\n-----END')),
            b'sig='+b64(signature), b'hr1='+b64(hr1), b'hmackr2=owned-local-challenge'])
        return (exchange(1, http(b'POST /youtube/accounts/applelogin1 HTTP/1.1', first, True), http(b'HTTP/1.0 200 OK', challenge))+
                exchange(2, http(b'POST /youtube/accounts/applelogin2 HTTP/1.1', second, True),
                    http(b'HTTP/1.0 503 Service Unavailable', b'Error=LocalDiagnosticOnly\n')))

    def public(self, key):
        return key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)

    def test_valid_raw_signature_and_original_encoding(self):
        report = audit.audit(self.fixture(), self.public(self.key))
        self.assertTrue(report['passed'])
        self.assertFalse(report['authenticationSucceeded'])
        self.assertTrue(report['certificateProof']['rawPKCS1DigestMatches'])
        self.assertTrue(report['guestKeyProof']['matchesCertificateKey'])
        safari = exchange(99, b'GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n', http(b'HTTP/1.0 200 OK', b'owned page'))
        self.assertTrue(audit.audit(safari+self.fixture())['passed'])
        for value in (b'YWJjZA==', b'YWJjZA..', b'YWJjZA%3D%3D', b'YWJjZA'):
            self.assertEqual(audit.web64(value), b'abcd')
        for invalid in (b'YWJjZB==', b'YWJjZA=', b'+w==', b'%2Bw==', b'YWJjZA%3', b'YWJjZA===' ):
            with self.assertRaises(ValueError):
                audit.web64(invalid)
        self.assertEqual(audit.percent(b'a+b'), b'a+b')

    def test_certificate_mismatch_stays_failure(self):
        report = audit.audit(self.fixture(signing=self.other), self.public(self.other))
        self.assertFalse(report['passed'])
        self.assertFalse(report['certificateProof']['rawPKCS1DigestMatches'])
        self.assertTrue(report['guestKeyProof']['rawPKCS1DigestMatches'])
        self.assertFalse(report['guestKeyProof']['matchesCertificateKey'])

    def test_nonce_signature_and_padding_changes_fail(self):
        for kwargs in ({'changed_nonce': True}, {'changed_signature': True}, {'digest_info': True}):
            report = audit.audit(self.fixture(**kwargs))
            self.assertFalse(report['passed'], kwargs)
        log = self.fixture().replace(b'WEATHER_RESPONSE_SENT 00000002', b'WEATHER_RESPONSE_SENT 00000003')
        with self.assertRaises(ValueError):
            audit.audit(log)

    def test_truncation_framing_and_pair_ambiguity(self):
        log = self.fixture()
        # Every prefix cutting the final block/receipt refuses partial evidence.
        start = log.rfind(b'WEATHER_RESPONSE_BYTES')
        for end in range(start, len(log)-1):
            with self.assertRaises(ValueError, msg=str(end)):
                audit.audit(log[:end])
        for bad in (log+log, log.replace(b'WEATHER_REQUEST_BYTES 000000', b'WEATHER_REQUEST_BYTES 000001', 1),
                    log.replace(b'D ', b'D z', 1), log.replace(b'WEATHER_RESPONSE_SENT ', b'UNSENT ', 1)):
            with self.assertRaises(ValueError):
                audit.audit(bad)
        for wire in (b'POST / HTTP/1.0\r\nContent-Length: 1\r\n\r\nxextra',
                     b'HTTP/1.0 200 OK\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n'):
            with self.assertRaises(ValueError):
                audit.http(wire)


if __name__ == '__main__':
    unittest.main(verbosity=2)
