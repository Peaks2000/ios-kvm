#!/usr/bin/env python3
"""Offline checks for preserving local trees and rejecting corrupt GPU source inputs."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('gpu_setup', Path(__file__).with_name('setup-gpu.py'))
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


class SourceInputs(unittest.TestCase):
    def test_corrupt_cached_archive_never_extracts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / '.local').mkdir()
            archive = root / '.local' / f'mesa-{setup.MESA_VERSION}.tar.xz'
            archive.write_bytes(b'corrupt archive')
            with patch.object(setup, 'ROOT', root), patch.object(setup, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'Cached Mesa archive checksum mismatch'):
                    setup.prepare_sources()
                run.assert_not_called()
            self.assertFalse(archive.with_suffix('').exists())
            self.assertEqual(list(archive.parent.iterdir()), [archive])

    def test_corrupt_download_is_not_promoted_or_left_behind(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def download(*arguments):
                self.assertEqual(arguments[0], 'curl')
                Path(arguments[-1]).write_bytes(b'corrupt download')
            with patch.object(setup, 'ROOT', root), patch.object(setup, 'run', side_effect=download):
                with self.assertRaisesRegex(RuntimeError, 'Mesa archive checksum mismatch'):
                    setup.prepare_sources()
            self.assertEqual(list((root / '.local').iterdir()), [])

    def test_unexpected_existing_revision_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mesa = root / '.local' / f'mesa-{setup.MESA_VERSION}'
            mesa.mkdir(parents=True)
            (mesa / 'meson.build').write_text('existing mesa')
            virgl = root / '.local/virglrenderer'
            virgl.mkdir()
            sentinel = virgl / 'local-work'
            sentinel.write_text('keep this')
            with patch.object(setup, 'ROOT', root), \
                 patch.object(setup.subprocess, 'check_output', return_value='another-revision\n'), \
                 patch.object(setup, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'No files reset'):
                    setup.prepare_sources()
                run.assert_not_called()
            self.assertEqual(sentinel.read_text(), 'keep this')
            self.assertEqual((mesa / 'meson.build').read_text(), 'existing mesa')


if __name__ == '__main__':
    unittest.main()
