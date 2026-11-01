#!/usr/bin/env python3
import unittest
from readback_wait_policy import BASE, PREVIOUS, CURRENT, patch

class WaitPolicyTests(unittest.TestCase):
    def test_fresh_and_existing_experiment(self):
        for old in (BASE, PREVIOUS, CURRENT):
            text = 'before\n' + old + '(args);\nafter'
            expected = 'before\n' + CURRENT + '(args);\nafter'
            self.assertEqual(patch(text), expected)
            self.assertEqual(patch(patch(text)), expected)

    def test_unrecognized_or_ambiguous_source(self):
        for text in ('unexpected source', BASE + '\n' + BASE, CURRENT + '\n' + CURRENT):
            with self.assertRaises(ValueError): patch(text)

if __name__ == '__main__': unittest.main()
