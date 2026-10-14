import importlib.util
import pathlib
import struct
import unittest

spec=importlib.util.spec_from_file_location('native_host',pathlib.Path(__file__).parents[1]/'tools/NativeSpatial/native_host.py')
host=importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)

class CaptureTests(unittest.TestCase):
    def record(self): return dict(count=1,frames=[128],captureQpcNs=1000000000,sequence=7,block='0x1234')
    def test_wire_round_trip(self):
        data=bytes(32+128*4); packet=host.encode(self.record(),data)
        fields=struct.unpack('<IIQQQIIII',packet[:48])
        self.assertEqual(fields,(0x3150534e,len(data),0x1234,7,1000000000,48000,1,128,0))
        self.assertEqual(packet[48:],data)
    def test_lengths_and_bounds(self):
        for replacement in [dict(count=0),dict(count=161),dict(frames=[0]),dict(frames=[4097]),dict(frames=[128,64]),dict(sequence=-1),dict(captureQpcNs=0)]:
            with self.subTest(replacement=replacement),self.assertRaises(ValueError):host.encode(self.record()|replacement,bytes(544))
        with self.assertRaises(ValueError):host.encode(self.record(),bytes(543))
    def test_file_input_removed(self):
        self.assertFalse(hasattr(host,'replay'))
        self.assertFalse(hasattr(host,'archive_packets'))

if __name__=='__main__':unittest.main()
