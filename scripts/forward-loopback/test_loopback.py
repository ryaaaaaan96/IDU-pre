import importlib.util
import json
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('loopback', Path(__file__).with_name('run.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        config = json.loads(Path(__file__).with_name('default.json').read_text())
        self.slave = m.Slave(config['slaves']['1'])

    def request(self, data):
        result = self.slave.reply(bytes.fromhex(data))
        if result:
            self.assertEqual(m.crc16(result[:-2]), int.from_bytes(result[-2:], 'little'))
        return result

    def test_known_crc(self):
        self.assertEqual(m.frame(bytes.fromhex('10 06 7b 0c 01 41')).hex(), '10067b0c014193cc')

    def test_read_write_readback(self):
        self.assertEqual(self.request('01 03 9c 40 00 01')[3:5], m.words([100]))
        self.request('01 06 9c 40 01 41')
        self.assertEqual(self.request('01 03 9c 40 00 01')[3:5], m.words([321]))

    def test_signed_multiwrite(self):
        self.request('01 10 9c 41 00 02 04 fb 2e ff ff')
        self.assertEqual(self.request('01 03 9c 41 00 02')[3:7], m.words([64302, 65535]))

    def test_offline_reconnect(self):
        self.slave.online = False
        self.assertIsNone(self.request('01 06 9c 40 01 41'))
        self.assertEqual(self.slave.holding[40000], 100)
        self.slave.online = True
        self.assertIsNotNone(self.request('01 04 1f d6 00 02'))

    def test_invalid_multiwrite_atomic(self):
        self.assertEqual(self.request('01 10 9c 42 00 02 04 00 01 00 02')[:3], bytes([1, 0x90, 2]))
        self.assertEqual(self.slave.holding[40002], 0)

    def test_invalid_count(self):
        self.assertEqual(self.request('01 03 9c 40 00 00')[:3], bytes([1, 0x83, 3]))


if __name__ == '__main__':
    unittest.main()
