"""Audit retained own-gateway auth exchanges; never contacts a service.

Requires cryptography for public certificate/key parsing. RSA raw verification
uses the recovered PKCS#1 v1.5 block, not a library's implicit DigestInfo wrapper.
A passed proof is NOT authentication success: the gateway deliberately returns503.
"""
import argparse
import base64
import hashlib
import json
import re
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

MAX_LOG = 16 * 1024 * 1024
MAX_WIRE = 32768


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def exchanges(log):
    require(len(log) <= MAX_LOG, 'log cap exceeded')
    lines = log.decode('ascii').splitlines()
    result, active, i = [], None, 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if line.startswith('WEATHER_REQUEST_BEGIN '):
            require(active is None, 'unfinished exchange before next request')
            match = re.fullmatch(r'WEATHER_REQUEST_BEGIN ([0-9a-f]{8}) ([0-9a-f]{8})', line)
            require(match is not None, 'malformed begin')
            active = {'serial': int(match[1], 16)}
        elif line.startswith(('WEATHER_REQUEST_BYTES ', 'WEATHER_RESPONSE_BYTES ')):
            require(active is not None, 'body without request begin')
            match = re.fullmatch(r'(WEATHER_REQUEST_BYTES|WEATHER_RESPONSE_BYTES) ([0-9a-f]{8}) 00000000', line)
            require(match is not None, 'malformed byte header')
            count = int(match[2], 16)
            require(0 < count <= MAX_WIRE, 'wire length outside cap')
            key = 'request' if match[1] == 'WEATHER_REQUEST_BYTES' else 'response'
            require(key not in active, 'duplicate byte block')
            require(key == 'request' or 'request' in active, 'response precedes request')
            data = bytearray()
            while len(data) < count:
                require(i < len(lines), 'truncated byte block')
                chunk = re.fullmatch(r'D ([0-9a-f]+)', lines[i])
                require(chunk is not None, 'missing hex bytes')
                require(len(chunk[1]) % 2 == 0, 'odd hex length')
                decoded = bytes.fromhex(chunk[1])
                require(len(decoded) == min(32, count-len(data)), 'hex row length mismatch')
                data.extend(decoded)
                i += 1
            require(i == len(lines) or not lines[i].startswith('D '), 'excess byte rows')
            active[key] = bytes(data)
        elif line.startswith('WEATHER_RESPONSE_SENT '):
            match = re.fullmatch(r'WEATHER_RESPONSE_SENT ([0-9a-f]{8}) ([0-9a-f]{8})', line)
            require(match is not None and active is not None, 'invalid sent receipt')
            require('request' in active and 'response' in active, 'sent without complete bodies')
            require(int(match[1], 16) == active['serial'] and int(match[2], 16) == len(active['response']), 'sent receipt mismatch')
            result.append(active)
            active = None
    require(active is None, 'unfinished final exchange')
    return result


def http(wire):
    require(len(wire) <= MAX_WIRE and b'\r\n\r\n' in wire, 'missing HTTP header')
    header, body = wire.split(b'\r\n\r\n', 1)
    require(len(header) <= 4096, 'HTTP header too large')
    lines = header.split(b'\r\n')
    headers = {}
    for row in lines[1:]:
        require(b':' in row, 'invalid HTTP header line')
        key, value = row.split(b':', 1)
        require(re.fullmatch(rb'[A-Za-z0-9_-]+', key) is not None, 'invalid header name')
        key = key.lower()
        require(key not in headers, 'duplicate HTTP header')
        require(all(32 <= v <= 126 or v == 9 for v in value), 'invalid header value')
        headers[key] = value.strip(b' \t')
    require(b'transfer-encoding' not in headers, 'unsupported transfer encoding')
    value = headers.get(b'content-length', b'0' if lines[0].startswith(b'GET ') else b'')
    require(re.fullmatch(rb'[0-9]{1,6}', value) is not None and int(value) == len(body), 'Content-Length mismatch')
    return lines[0], headers, body


def percent(value):
    require(re.search(rb'%(?![0-9A-Fa-f]{2})', value) is None, 'bad percent encoding')
    # Deliberately no application/x-www-form-urlencoded '+' to space conversion.
    return re.sub(rb'%([0-9A-Fa-f]{2})', lambda m: bytes([int(m[1], 16)]), value)


def web64(value):
    data = percent(value)
    require(re.fullmatch(rb'[A-Za-z0-9_-]+[=.]{0,2}', data) is not None, 'invalid web-safe base64')
    stripped = data.rstrip(b'=.')
    require(len(stripped) % 4 != 1, 'invalid base64 length')
    padding = (-len(stripped)) % 4
    require(len(data)-len(stripped) in (0, padding), 'invalid base64 padding')
    decoded = base64.b64decode(stripped+b'='*padding, altchars=b'-_', validate=True)
    require(base64.urlsafe_b64encode(decoded).rstrip(b'=') == stripped, 'noncanonical base64 bits')
    return decoded


def fields(body, separator=b'&'):
    result = {}
    rows = body.split(separator)
    if separator == b'\n' and rows[-1:] == [b'']:
        rows.pop()
    for row in rows:
        require(b'=' in row, 'missing field assignment')
        key, value = row.split(b'=', 1)
        require(key and value and key not in result, 'empty/duplicate field')
        result[key] = value
    return result


def public_metadata(key):
    require(isinstance(key, rsa.RSAPublicKey), 'non-RSA public key')
    require(512 <= key.key_size <= 8192, 'RSA size outside audit bounds')
    encoded = key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return {'bits': key.key_size, 'spkiSHA256': sha(encoded)}


def verify_raw(key, signature, digest):
    meta = public_metadata(key)
    numbers = key.public_numbers()
    width = (numbers.n.bit_length()+7)//8
    integer = int.from_bytes(signature, 'big')
    valid_size = len(signature) == width and integer < numbers.n
    recovered = pow(integer, numbers.e, numbers.n).to_bytes(width, 'big') if valid_size else b''
    # Exact EMSA block for SecKeyRawSign(key, 1, digest, 20, ...).
    expected = b'\x00\x01'+b'\xff'*(width-len(digest)-3)+b'\x00'+digest
    return dict(meta, signatureLengthValid=valid_size,
                rawPKCS1DigestMatches=valid_size and width-len(digest)-3 >= 8 and recovered == expected,
                recoveredBlockSHA256=sha(recovered) if valid_size else None)


def audit(log, guest_public_key=None):
    pairs = []
    all_rows = exchanges(log)
    for row in all_rows:
        first, headers, body = http(row['request'])
        if first.startswith(b'POST /youtube/accounts/applelogin'):
            match = re.fullmatch(rb'POST /youtube/accounts/applelogin([12]) HTTP/1\.[01]', first)
            require(match is not None, 'unknown auth request target')
            require(headers.get(b'host', b'').lower() in (b'dm5.google.com', b'dm5.google.com:80'), 'wrong auth host')
            require(headers.get(b'content-type', b'').lower() == b'application/x-www-form-urlencoded', 'wrong auth content type')
            response_line, _, response_body = http(row['response'])
            pairs.append((int(match[1]), fields(body), response_line, response_body, row))
    require([p[0] for p in pairs] == [1, 2], 'exactly one ordered phase1/phase2 pair required')
    _, first, status1, reply1, row1 = pairs[0]
    _, second, status2, reply2, row2 = pairs[1]
    require(set(first) == {b'hr1'}, 'phase1 field set')
    require(set(second) == {b'r1', b'r2', b'hr1', b'hmackr2', b'cert', b'sig'}, 'phase2 field set')
    require(status1 in (b'HTTP/1.0 200 OK', b'HTTP/1.1 200 OK'), 'phase1 unsuccessful')
    require(status2 in (b'HTTP/1.0 503 Service Unavailable', b'HTTP/1.1 503 Service Unavailable') and b'Error=LocalDiagnosticOnly\n' in reply2 and b'Auth=' not in reply2, 'phase2 not explicit diagnostic refusal')
    challenge = fields(reply1, b'\n')
    require(set(challenge) == {b'r2', b'hmackr2'}, 'challenge field set')
    r1, r2 = web64(second[b'r1']), web64(second[b'r2'])
    h1, h2 = web64(first[b'hr1']), web64(second[b'hr1'])
    require(len(r1) == len(r2) == len(h1) == len(h2) == 20, 'nonce/hash length')
    nonce_checks = {'hr1MatchesSHA1r1': h1 == h2 == hashlib.sha1(r1).digest(),
                    'r2MatchesChallenge': r2 == web64(challenge[b'r2']),
                    'hmackr2MatchesChallenge': percent(second[b'hmackr2']) == challenge[b'hmackr2']}
    pem = web64(second[b'cert'])
    require(re.fullmatch(rb'-----BEGIN CERTIFICATE-----\r?\n[A-Za-z0-9+/=\r\n]+\r?\n-----END CERTIFICATE-----\r?\n?', pem) is not None, 'not exactly one PEM certificate')
    # The original encoder emits an extra blank line before END. Decode only
    # CR/LF separators permitted by the PEM grammar above; preserve raw evidence.
    interior = pem.split(b'-----BEGIN CERTIFICATE-----', 1)[1].split(b'-----END CERTIFICATE-----', 1)[0]
    der = base64.b64decode(interior.replace(b'\r', b'').replace(b'\n', b''), validate=True)
    certificate = x509.load_der_x509_certificate(der)
    require(certificate.public_bytes(serialization.Encoding.DER) == der, 'noncanonical/trailing certificate DER')
    signature = web64(second[b'sig'])
    digest = hashlib.sha1(r2+r1).digest()
    cert_proof = verify_raw(certificate.public_key(), signature, digest)
    guest_proof = None
    if guest_public_key is not None:
        guest_key = serialization.load_pem_public_key(guest_public_key)
        guest_proof = verify_raw(guest_key, signature, digest)
        guest_proof['matchesCertificateKey'] = guest_proof['spkiSHA256'] == cert_proof['spkiSHA256']
    passed = all(nonce_checks.values()) and cert_proof['rawPKCS1DigestMatches']
    if guest_proof is not None:
        passed = passed and guest_proof['rawPKCS1DigestMatches'] and guest_proof['matchesCertificateKey']
    return {'passed': passed, 'authenticationSucceeded': False,
            'scope': 'Retained challenge/signature diagnostic; HTTP503 is not authentication success. No certificate trust-chain validation.',
            'logSHA256': sha(log), 'completeExchanges': len(all_rows), 'nonceChecks': nonce_checks,
            'certificatePEM_SHA256': sha(pem), 'certificateDER_SHA256': sha(certificate.public_bytes(serialization.Encoding.DER)),
            'signatureSHA256': sha(signature), 'signedDigestSHA256': sha(digest),
            'certificateProof': cert_proof, 'guestKeyProof': guest_proof,
            'authExchanges': [{'serial': r['serial'], 'requestSHA256': sha(r['request']), 'responseSHA256': sha(r['response'])} for r in (row1, row2)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--guest-public-key', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        require(args.log.stat().st_size <= MAX_LOG, 'log cap exceeded')
        key = None
        if args.guest_public_key:
            require(args.guest_public_key.stat().st_size <= 32768, 'public key cap exceeded')
            key = args.guest_public_key.read_bytes()
        report = audit(args.log.read_bytes(), key)
    except (ValueError, UnicodeError) as error:
        report = {'passed': False, 'authenticationSucceeded': False, 'error': str(error)}
    text = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
