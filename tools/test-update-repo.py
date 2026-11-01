#!/usr/bin/env python3
"""Exercise updater in disposable repositories, never the user's device."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import fcntl

spec = importlib.util.spec_from_file_location('updater', Path(__file__).with_name('update-repo.py'))
u = importlib.util.module_from_spec(spec)
spec.loader.exec_module(u)


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.DEVNULL).decode().strip()


class UpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.remote = base / 'upstream'
        self.remote.mkdir()
        git(self.remote, 'init', '-b', 'master')
        git(self.remote, 'config', 'user.name', 'Test')
        git(self.remote, 'config', 'user.email', 'test@example.invalid')
        (self.remote / '.gitignore').write_text('.local/\n')
        (self.remote / 'source').write_text('base\n')
        git(self.remote, 'add', '.')
        git(self.remote, 'commit', '-m', 'base')
        self.local = base / 'local'
        git(base, 'clone', str(self.remote), str(self.local))
        git(self.local, 'config', 'user.name', 'Test')
        git(self.local, 'config', 'user.email', 'test@example.invalid')
        u.ROOT = self.local
        (self.local / '.local').mkdir()
        self.disk = self.local / '.local/data.hfs.img'
        self.disk.write_bytes(b'private NAND\0')

    def upstream(self, path='new', text='new'):
        (self.remote / path).write_text(text)
        git(self.remote, 'add', '.')
        git(self.remote, 'commit', '-m', 'upstream')

    def test_rebase_and_local_edits(self):
        (self.local / 'mine').write_text('local commit')
        git(self.local, 'add', 'mine')
        git(self.local, 'commit', '-m', 'mine')
        (self.local / 'source').write_text('local preference\n')
        git(self.local, 'add', 'source')
        self.upstream()
        u.update(str(self.remote), 'master')
        self.assertEqual((self.local / 'source').read_text(), 'local preference\n')
        self.assertEqual(git(self.local, 'diff', '--cached', '--name-only'), 'source')
        self.assertEqual(self.disk.read_bytes(), b'private NAND\0')
        self.assertTrue(list((self.local / '.local/updates').glob('*/history.bundle')))
        self.assertTrue((self.local / 'mine').exists())

    def test_conflict_aborts(self):
        (self.local / 'source').write_text('mine\n')
        git(self.local, 'commit', '-am', 'mine')
        old = git(self.local, 'rev-parse', 'HEAD')
        self.upstream('source', 'theirs\n')
        with self.assertRaises(RuntimeError):
            u.update(str(self.remote), 'master')
        self.assertEqual(git(self.local, 'rev-parse', 'HEAD'), old)
        self.assertEqual((self.local / 'source').read_text(), 'mine\n')

    def test_ignored_collision_refused(self):
        (self.local / '.git/info/exclude').write_text('new\n')
        (self.local / 'new').write_text('private')
        self.upstream()
        with self.assertRaises(RuntimeError):
            u.update(str(self.remote), 'master')
        self.assertEqual((self.local / 'new').read_text(), 'private')

    def test_active_vm_refused(self):
        with (self.local / '.local/vm-session.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            with self.assertRaises(RuntimeError):
                u.update(str(self.remote), 'master')

    def test_private_incoming_refused(self):
        (self.remote / 'firmware').mkdir()
        self.upstream('firmware/private', 'unexpected')
        old = git(self.local, 'rev-parse', 'HEAD')
        with self.assertRaises(RuntimeError):
            u.update(str(self.remote), 'master')
        self.assertEqual(git(self.local, 'rev-parse', 'HEAD'), old)

    def test_local_edit_conflict_retains_recovery(self):
        (self.local / 'source').write_text('unstaged preference\n')
        self.upstream('source', 'upstream preference\n')
        with self.assertRaisesRegex(RuntimeError, 'local edits conflict'):
            u.update(str(self.remote), 'master')
        self.assertIn('unstaged preference', git(self.local, 'show', 'stash:source'))
        self.assertTrue(list((self.local / '.local/updates').glob('*/unstaged.patch')))
        self.assertEqual(self.disk.read_bytes(), b'private NAND\0')

    def test_check_only(self):
        old = git(self.local, 'rev-parse', 'HEAD')
        self.upstream()
        u.update(str(self.remote), 'master', True)
        self.assertEqual(git(self.local, 'rev-parse', 'HEAD'), old)


if __name__ == '__main__':
    unittest.main()
