#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('stacks', Path(__file__).with_name('symbolize-stacks.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class StackTests(unittest.TestCase):
    def test_separate_respring_slides_and_threads(self):
        records = m.snapshots('''noise
ustacks: backboardd[13] 2 threads, shared cache slide 0x1f9ac000, uptime 42
ustacks:  thread 0x10 "com.apple.coreanimation.display.primary": 0x211b51030 0x0
ustacks:  thread 0x11: 0x100000000
ustacks: backboardd[799] 1 threads, shared cache slide 0x4f540000, uptime 3165
ustacks:  thread 0x313d "com.apple.coreanimation.display.primary": 0x211b51030
''')
        self.assertEqual([r['pid'] for r in records], [13, 799])
        self.assertEqual([r['slide'] for r in records], [0x1f9ac000, 0x4f540000])
        self.assertEqual(records[0]['threads'][0]['addresses'], [0x211b51030, 0])
        self.assertEqual(records[0]['threads'][1]['name'], '')
        self.assertEqual(len(records[1]['threads']), 1)

    def test_no_header_does_not_guess_slide(self):
        self.assertEqual(m.snapshots('ustacks:  thread 0x11: 0x1234'), [])


if __name__ == '__main__':
    unittest.main()
