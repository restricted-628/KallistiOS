"""Adversarial format/authoring tests using original synthetic PCM only."""
import copy
import io
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
import zlib

import aica_bank as bank


def seal(data):
    data = bytearray(data)
    data[40:44] = bytes(4)
    data[40:44] = zlib.crc32(data).to_bytes(4, 'little')
    return bytes(data)


def golden():
    # Independent wire construction from the written offsets, not the builder.
    b = bytearray(160)
    b[:8] = b'KOSABK\0\0'
    b[8:12] = bytes.fromhex('01004000')
    for off, value in {12:160, 16:1, 20:1, 24:64, 28:96, 32:128, 36:32,
                       64:128, 68:8, 72:4, 76:22050}.items():
        b[off:off+4] = value.to_bytes(4, 'little')
    b[88] = 2
    b[104:108] = bytes((127, 1, 127, 60))
    b[110:112] = bytes((255, 128))
    b[112] = b[115] = 31
    b[118] = 15
    b[128:136] = bytes.fromhex('0080ffff0000ff7f')
    return seal(b)


def write_wav(path, width=2, channels=1, rate=22050, pcm=None):
    if pcm is None:
        pcm = bytes.fromhex('0080ffff0000ff7f') if width == 2 else bytes((0, 127, 128, 255))
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(width)
        wav.setframerate(rate)
        wav.writeframes(pcm)


class BankTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        write_wav(self.directory / 'tone.wav')
        self.manifest = {'version': 1, 'samples': [{'name': 'tone', 'wav': 'tone.wav'}],
                         'zones': [{'sample': 'tone'}]}

    def test_independent_wire_and_pcm(self):
        self.assertEqual(bank.build(self.manifest, self.directory), golden())
        info = bank.validate(golden())
        self.assertEqual(info['samples'][0]['frames'], 4)
        self.assertEqual(info['zones'][0]['root_key'], 60)

    def test_pcm8_signed_conversion(self):
        write_wav(self.directory / 'tone.wav', width=1)
        data = bank.build(self.manifest, self.directory)
        self.assertEqual(data[128:132], bytes((128, 255, 0, 127)))
        self.assertEqual(bank.validate(data)['samples'][0]['encoding'], 1)

    def test_loop_key_velocity_and_drum(self):
        self.manifest['samples'][0].update(loop_start=1, loop_end=4)
        self.manifest['zones'] = [
            {'sample':'tone', 'velocity_high':63, 'fine_cents':-100},
            {'sample':'tone', 'velocity_low':64, 'fine_cents':100},
            {'sample':'tone', 'drum':1, 'key_low':36, 'key_high':36},
        ]
        result = bank.validate(bank.build(self.manifest, self.directory))
        self.assertEqual(result['samples'][0]['loop_end'], 4)
        self.assertEqual(len(result['zones']), 3)
        self.manifest['zones'][1]['velocity_low'] = 63
        with self.assertRaisesRegex(bank.BankError, 'overlapping'):
            bank.build(self.manifest, self.directory)

    def test_all_truncations_and_single_bit_corruption(self):
        original = golden()
        for size in range(len(original)):
            with self.subTest(truncate=size), self.assertRaises(bank.BankError):
                bank.validate(original[:size])
        for i in range(len(original)):
            for bit in range(8):
                data = bytearray(original)
                data[i] ^= 1 << bit
                with self.subTest(byte=i, bit=bit), self.assertRaises(bank.BankError):
                    bank.validate(data)

    def test_resealed_semantic_corruption(self):
        # Recompute CRC so each mutation reaches structural/semantic validation.
        cases = [(12,4,159),(16,4,0),(16,4,257),(20,4,1025),(24,4,0xffffffff),
                 (28,4,64),(32,4,0xffffffe0),(36,4,0),(44,4,1),(48,1,1),
                 (64,4,64),(64,4,129),(68,4,0xffffffff),(72,4,65535),(72,4,65536),
                 (76,4,0),(76,4,192001),(80,4,1),(84,4,4),(88,2,3),
                 (90,2,2),(92,4,1),(96,4,1),(100,1,128),(101,1,128),
                 (102,1,2),(103,1,128),(104,1,128),(105,1,0),(106,1,128),
                 (107,1,128),(108,2,101),(108,2,65435),(112,1,32),
                 (113,1,32),(114,1,32),(115,1,32),(116,1,32),
                 (117,1,16),(118,1,16),(119,1,16),(120,1,16),(121,1,1),
                 (136,1,1)]
        for off, width, value in cases:
            b = bytearray(golden())
            b[off:off+width] = value.to_bytes(width, 'little')
            with self.subTest(offset=off, value=value), self.assertRaises(bank.BankError):
                bank.validate(seal(b))

    def test_overlapping_or_trailing_payload(self):
        self.manifest['samples'].append({'name':'other', 'wav':'tone.wav'})
        data = bytearray(bank.build(self.manifest, self.directory))
        data[96:100] = data[64:68]
        with self.assertRaises(bank.BankError):
            bank.validate(seal(data))
        b = bytearray(golden()) + bytes(32)
        struct.pack_into('<I', b, 12, len(b))
        struct.pack_into('<I', b, 36, 64)
        with self.assertRaisesRegex(bank.BankError, 'trailing'):
            bank.validate(seal(b))

    def test_manifest_rejections(self):
        mutations = [lambda m:m.update(version=True), lambda m:m.update(typo=1),
                     lambda m:m.update(samples=[]), lambda m:m.update(zones=[]),
                     lambda m:m['samples'].append(m['samples'][0]),
                     lambda m:m['samples'][0].update(loop_start=0),
                     lambda m:m['samples'][0].update(loop_start=2, loop_end=1),
                     lambda m:m['zones'][0].update(sample='missing'),
                     lambda m:m['zones'][0].update(volume=True),
                     lambda m:m['zones'][0].update(volume=256),
                     lambda m:m['zones'][0].update(volume=1.5),
                     lambda m:m['zones'][0].update(key_low=90, key_high=89)]
        for mutate in mutations:
            m = copy.deepcopy(self.manifest)
            mutate(m)
            with self.assertRaises(bank.BankError):
                bank.build(m, self.directory)

    def test_wav_rejections_and_maximum_frame(self):
        for width, channels, rate, pcm in [(2,2,22050,None),(3,1,22050,b'\0'*12),
                                          (2,1,22050,b''),(2,1,192001,None),
                                          (2,1,22050,b'\0'*131070),
                                          (2,1,22050,b'\0'*131072)]:
            write_wav(self.directory/'tone.wav', width, channels, rate, pcm)
            with self.assertRaises(bank.BankError):
                bank.build(self.manifest, self.directory)
        write_wav(self.directory/'tone.wav', pcm=b'\0'*131068)
        self.assertEqual(bank.validate(bank.build(self.manifest, self.directory))
                         ['samples'][0]['frames'], 65534)
        # RIFF header promises sample bytes that do not exist.
        p = self.directory/'tone.wav'
        p.write_bytes(p.read_bytes()[:-1])
        with self.assertRaisesRegex(bank.BankError, 'truncated'):
            bank.build(self.manifest, self.directory)

    def test_payload_budget(self):
        write_wav(self.directory/'tone.wav', pcm=b'\0'*131068)
        self.manifest['samples'] = [{'name':str(i), 'wav':'tone.wav'} for i in range(15)]
        self.manifest['zones'][0]['sample'] = '0'
        with self.assertRaisesRegex(bank.BankError, 'budget'):
            bank.build(self.manifest, self.directory)

    def test_bounded_resealed_random_mutation(self):
        rng = random.Random(0xA1CA)
        for _ in range(3000):
            b = bytearray(golden())
            for _ in range(rng.randrange(1, 9)):
                b[rng.randrange(len(b))] = rng.randrange(256)
            try:
                bank.validate(seal(b))
            except bank.BankError:
                pass  # A structured rejection, never struct/index/type failure.

    def test_cli_valid_failure_and_no_overwrite(self):
        manifest = self.directory/'bank.json'
        manifest.write_text(json.dumps(self.manifest))
        out = self.directory/'bank.abk'
        def run(*args):
            return subprocess.run([sys.executable, str(Path(bank.__file__)), *map(str,args)],
                                  capture_output=True)
        self.assertEqual(run('build',manifest,out).returncode, 0)
        self.assertEqual(run('check',out).returncode, 0)
        self.assertEqual(out.read_bytes(), golden())
        self.assertEqual(run('build',manifest,out).returncode, 1)
        self.assertEqual(out.read_bytes(), golden())
        for content in ['{"version":1,"version":1}', '['*2000,
                        json.dumps({'version':1}), ' '*(bank.MAX_MANIFEST+1)]:
            manifest.write_text(content)
            new = self.directory/'bad.abk'
            self.assertEqual(run('build',manifest,new).returncode, 1)
            self.assertFalse(new.exists())


if __name__ == '__main__':
    unittest.main()
