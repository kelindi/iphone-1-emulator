import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
NAME = b"\x09iphone-wu\x05apple\x03com\0"


def query(name=NAME, kind=1, edns=b"", flags=0x100):
    return struct.pack("!6H", 0x1234, flags, 1, 0, 0, bool(edns)) + name + struct.pack("!2H", kind, 1) + edns


class DNSPacket(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        lib = Path(cls.directory.name) / "dns.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC", str(HERE / "dns_packet.c"), "-o", str(lib)], check=True)
        cls.fn = ctypes.CDLL(str(lib)).own_dns_response
        cls.fn.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def response(self, packet, success=True, cap=1232):
        source = ctypes.create_string_buffer(packet)
        output = ctypes.create_string_buffer(b"\xa5" * 1248)
        written = ctypes.c_uint(999)
        result = self.fn(source, len(packet), output, cap, ctypes.byref(written))
        self.assertEqual(result, int(success))
        self.assertEqual(source.raw[:len(packet)], packet)
        self.assertEqual(output.raw[cap:1248], b"\xa5" * (1248-cap))
        if not success:
            self.assertEqual(written.value, 0)
            self.assertEqual(output.raw[:1248], b"\xa5" * 1248)
        return output.raw[:written.value]

    def test_exact_a(self):
        expected = struct.pack("!6H", 0x1234, 0x8500, 1, 1, 0, 0) + NAME + b"\0\1\0\1" + bytes.fromhex("c00c000100010000003c00047f000001")
        self.assertEqual(self.response(query()), expected)
        self.assertEqual(self.response(query(flags=0x30))[2:4], b"\x84\x10") # AD cleared, CD echoed

    def test_auth_name_is_exact_and_bounded(self):
        auth=b"\x03dm5\x06google\x03com\0"
        self.assertEqual(self.response(query(name=auth))[-4:], b"\x7f\0\0\1")
        self.assertEqual(self.response(query(name=auth.upper()))[-4:], b"\x7f\0\0\1")
        self.assertEqual(self.response(query(name=auth,kind=28))[6:8], b"\0\0")
        for name in (b"\x03dm5\x0agoogle.com\0", b"\x0edm5.google.com\0",b"\x03dm6\x06google\x03com\0"):
            self.assertEqual(self.response(query(name=name))[3]&15,3)
        for end in range(len(query(name=auth))):
            self.response(query(name=auth)[:end],False)

    def test_case_aaaa_and_other_type(self):
        self.assertEqual(self.response(query(name=NAME.upper()))[-4:], b"\x7f\0\0\1")
        for kind in (28, 15, 255):
            r = self.response(query(kind=kind))
            self.assertEqual(r[:12], struct.pack("!6H", 0x1234, 0x8500, 1, 0, 0, 0))

    def test_unknown_nxdomain(self):
        for name in (b"\x03foo\0", b"\0", b"\x09iphone-wu\x05apple\x03com\x01x\0",
                     b"\x09iphone-wu\x09apple.com\0", b"\x13iphone-wu.apple.com\0"):
            self.assertEqual(self.response(query(name=name))[:12], struct.pack("!6H", 0x1234, 0x8503, 1, 0, 0, 0))

    def test_edns(self):
        opt = b"\0" + struct.pack("!HHIH", 41, 4096, 0x8000, 7) + struct.pack("!HH", 65001, 3) + b"abc"
        r = self.response(query(edns=opt))
        self.assertEqual(r[-11:], b"\0" + struct.pack("!HHIH", 41, 1232, 0x8000, 0))
        self.assertEqual(r[-15:-11], b"\x7f\0\0\1")
        for opt in (b"\0" + struct.pack("!HHIH", 41, 512, 0x10000, 0),
                    b"\0" + struct.pack("!HHIH", 41, 512, 0, 1) + b"x",
                    b"\0" + struct.pack("!HHIH", 41, 512, 0, 4) + b"\0\1\0\1"):
            self.response(query(edns=opt), False)

    def test_all_truncations(self):
        packet = query()
        for end in range(len(packet)):
            self.response(packet[:end], False)

    def test_framing_rejections(self):
        for flags in (0x8000, 0x800, 0x400, 0x200, 0x40, 1):
            self.response(query(flags=flags), False)
        for offset in (4, 6, 8, 10):
            p = bytearray(query());p[offset:offset+2] = b"\0\2"
            self.response(bytes(p), False)
        self.response(query()+b"extra", False)
        self.response(query()[:-1]+b"\3", False)
        self.response(query(name=b"\xc0\x0c"), False)
        self.response(query(name=b"\x40"+b"a"*64+b"\0"), False)
        self.response(query(name=(b"\x3f"+b"a"*63)*4+b"\0"), False)
        self.response(b"\0"*1233, False)

    def test_capacity(self):
        size = len(self.response(query()))
        for cap in (0, 12, size-1):
            self.response(query(), False, cap)
        self.assertEqual(len(self.response(query(), cap=size)), size)


if __name__ == "__main__":
    unittest.main()
