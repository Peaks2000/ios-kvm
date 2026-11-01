#!/usr/bin/env python3
"""Check paired GPU launch settings and rejection before QEMU; no VM starts."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from gpu_launch import boot_arguments, requested_mode


class Launch(unittest.TestCase):
    def test_unset_preserves_manual_arguments_byte_for_byte(self):
        value='  gpu_async=1 glenv=IOS_KVM_ASYNC_READBACK=marker  touch_lat=1 '
        self.assertEqual(boot_arguments(value,None),value)

    def test_both_halves_replace_conflicting_values_and_are_idempotent(self):
        for mode in ['0','1']:
            value='gpu_async=0 glenv=IOS_KVM_ASYNC_READBACK=marker touch_lat=1 gpu_async=1 glenv=OTHER=2 glenv=IOS_KVM_ASYNC_READBACK_EXTRA=1'
            expected=f'touch_lat=1 glenv=OTHER=2 glenv=IOS_KVM_ASYNC_READBACK_EXTRA=1 gpu_async={mode} glenv=IOS_KVM_ASYNC_READBACK={mode}'
            self.assertEqual(boot_arguments(value,mode),expected)
            self.assertEqual(boot_arguments(expected,mode),expected)

    def test_local_choice_and_explicit_environment_precedence(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            self.assertIsNone(requested_mode({},root))
            (root/'.local').mkdir();path=root/'.local/gpu-async'
            path.write_text('1\n');self.assertEqual(requested_mode({},root),'1')
            self.assertEqual(requested_mode({'IOS_KVM_ASYNC_READBACK':'0'},root),'0')
            path.write_text('bad')
            with self.assertRaisesRegex(ValueError,'must contain 0 or 1'):requested_mode({},root)
            self.assertEqual(requested_mode({'IOS_KVM_ASYNC_READBACK':'1'},root),'1')

    def test_bad_modes_never_start_qemu(self):
        for mode in ['','auto','marker','true','2']:
            env={**os.environ,'IOS_KVM_ASYNC_READBACK':mode,'QEMU_BINARY':'/does/not/exist'}
            result=subprocess.run(['bash',str(Path(__file__).with_name('run.sh'))],env=env,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('IOS_KVM_ASYNC_READBACK must be 0 or 1',result.stderr)
            self.assertNotIn('QEMU executable not found',result.stderr)

    def test_cli_passes_literal_shell_text_without_execution(self):
        env={**os.environ,'IOS_KVM_ASYNC_READBACK':'1','CMDLINE':'touch_lat=1 glenv=TEXT=$(false)'}
        result=subprocess.run([sys.executable,str(Path(__file__).with_name('gpu_launch.py'))],env=env,capture_output=True,text=True,check=True)
        self.assertEqual(result.stdout.strip(),'touch_lat=1 glenv=TEXT=$(false) gpu_async=1 glenv=IOS_KVM_ASYNC_READBACK=1')


if __name__=='__main__':unittest.main()
