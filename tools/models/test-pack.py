#!/usr/bin/env python3
"""Check pose removal and sub-part alignment without ROM-derived test data."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('pack', Path(__file__).with_name('pack-vf1.py'))
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class Packing(unittest.TestCase):
    def test_inverse_scaled_rotated_translated(self):
        m = [0, 2, 0, -3, 0, 0, 0, 0, 4, 10, 20, 30]
        p = [1, 2, 3]
        for a, b in zip(pack.transform(pack.inverse(m), pack.transform(m, p)), p):
            self.assertAlmostEqual(a, b)

    def test_relative_part_and_alignment(self):
        ref = dict(rec='test', tag='h0', m=pack.IDENTITY[:9] + [10, 20, 30], p=[], q=[])
        part = dict(rec='test', tag='h1', m=pack.IDENTITY[:9] + [12, 20, 30],
                    p=[[0, 0, 0], [1, 0, 0], [0, 1, 0]], q=[[0, 1, 2, 2, 31]])
        binding = dict(object='0x800000', tags=['h*'], reference='h0',
                       matrix=[2, 0, 0, 0, 2, 0, 0, 0, 2, 1, 0, 0])
        lines = pack.pack([ref, part], [binding]).splitlines()
        self.assertEqual(lines[:2], ['VF2MESH 1 1', '800000 1'])
        self.assertEqual(lines[2], 'ff0000 5 0 0 7 0 0 5 2 0')
        part['p'][0][0] = float('nan')
        with self.assertRaises(ValueError):
            pack.pack([ref, part], [binding])


if __name__ == '__main__':
    unittest.main()
