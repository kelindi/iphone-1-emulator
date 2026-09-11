"""Own activation identity consistency; no firmware images or private output."""
import unittest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa, padding, ec
from rootfs_work import _local_activation_data

class IdentityTests(unittest.TestCase):
    def verify_bundle(self, bundle, private):
        activation, ca, record = bundle[:3]
        activation = x509.load_pem_x509_certificate(activation)
        ca = x509.load_pem_x509_certificate(ca)
        device = x509.load_pem_x509_certificate(record['DeviceCertificate'])
        self.assertEqual(device.public_key().public_numbers(), private.public_key().public_numbers())
        ca.public_key().verify(device.signature, device.tbs_certificate_bytes,
                               padding.PKCS1v15(), device.signature_hash_algorithm)
        activation.public_key().verify(record['AccountTokenSignature'], record['AccountToken'],
                                       padding.PKCS1v15(), hashes.SHA1())
        message = b'own YouTube identity signing qualification'
        signed = private.sign(message, padding.PKCS1v15(), hashes.SHA1())
        device.public_key().verify(signed, message, padding.PKCS1v15(), hashes.SHA1())
    def test_supplied_key_reused_and_signed_certificate_matches(self):
        key = rsa.generate_private_key(public_exponent=65537, key_size=1024)
        bundle = _local_activation_data(device_key=key)
        self.assertEqual(len(bundle), 3)
        self.verify_bundle(bundle, key)
    def test_generated_keys_export_exact_original_pem_formats(self):
        bundle = _local_activation_data(include_device_keys=True)
        self.assertEqual(len(bundle), 4)
        keys = bundle[3]
        self.assertEqual(set(keys), {'device_private_key.pem','device_public_key.pem'})
        self.assertTrue(keys['device_private_key.pem'].startswith(b'-----BEGIN RSA PRIVATE KEY-----\n'))
        self.assertTrue(keys['device_public_key.pem'].startswith(b'-----BEGIN RSA PUBLIC KEY-----\n'))
        private = serialization.load_pem_private_key(keys['device_private_key.pem'], None)
        public = serialization.load_pem_public_key(keys['device_public_key.pem'])
        self.assertEqual(private.public_key().public_numbers(), public.public_numbers())
        self.verify_bundle(bundle, private)
    def test_legacy_return_contract(self):
        bundle = _local_activation_data()
        self.assertIsInstance(bundle, tuple)
        self.assertEqual(len(bundle), 3)
        self.assertEqual(set(bundle[2]), {'DeviceCertificate','AccountToken','AccountTokenSignature'})
    def test_reject_non_rsa_before_generation(self):
        with self.assertRaises(TypeError):
            _local_activation_data(device_key=ec.generate_private_key(ec.SECP256R1()))

if __name__ == '__main__': unittest.main()
