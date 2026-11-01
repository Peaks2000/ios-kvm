#!/usr/bin/env python3
"""CPU launch policy checks, including a real exec argv check; no VM is started."""
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

source = Path(__file__).with_name('qemu-cpu.py')
spec = importlib.util.spec_from_file_location('qemu_cpu', source)
cpu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpu)


class Policy(unittest.TestCase):
    def test_default_preserves_tcg(self):
        with patch.object(cpu.subprocess, 'run') as run:
            self.assertEqual(cpu.configuration({}, 'qemu'), ['-cpu','cortex-a76','-accel','tcg,tb-size=384'])
            run.assert_not_called()

    def test_tcg_cache_override_and_validation(self):
        self.assertEqual(cpu.configuration({'QEMU_TB_MB':'1024'},'qemu')[-1], 'tcg,tb-size=1024')
        for bad in ('0','-1','1024,thread=single','no'):
            with self.assertRaises(ValueError): cpu.configuration({'QEMU_TB_MB':bad},'qemu')

    def test_unknown_mode_never_falls_back(self):
        with self.assertRaises(ValueError): cpu.configuration({'QEMU_ACCEL':'auto'},'qemu')

    def test_x86_cannot_use_arm_kvm(self):
        with patch.object(cpu.platform,'system',return_value='Linux'), \
             patch.object(cpu.platform,'machine',return_value='x86_64'), \
             patch.object(cpu.os,'stat') as probe:
            with self.assertRaisesRegex(ValueError,'ARM64 Linux'): cpu.configuration({'QEMU_ACCEL':'kvm'},'qemu')
            probe.assert_not_called()

    def test_arm_without_kvm_device(self):
        with patch.object(cpu.platform,'system',return_value='Linux'), \
             patch.object(cpu.platform,'machine',return_value='aarch64'), \
             patch.object(cpu.os,'stat',side_effect=FileNotFoundError('no device')):
            with self.assertRaisesRegex(ValueError,'unavailable'): cpu.configuration({'QEMU_ACCEL':'kvm'},'qemu')

    def arm(self, access=True, mode=stat.S_IFCHR, accelerators='kvm\ntcg\n'):
        return (patch.object(cpu.platform,'system',return_value='Linux'),
                patch.object(cpu.platform,'machine',return_value='aarch64'),
                patch.object(cpu.os,'stat',return_value=types.SimpleNamespace(st_mode=mode)),
                patch.object(cpu.os,'access',return_value=access),
                patch.object(cpu.subprocess,'run',return_value=types.SimpleNamespace(stdout=accelerators)))

    def test_kvm_policy_uses_host_cpu_without_tcg_options(self):
        a,b,c,d,e=self.arm()
        with a,b,c,d,e:
            self.assertEqual(cpu.configuration({'QEMU_ACCEL':'kvm','QEMU_TB_MB':'1024'},'qemu'),
                             ['-cpu','host','-accel','kvm'])

    def test_kvm_permissions_type_and_compiled_backend(self):
        for options in ({'access':False},{'mode':stat.S_IFREG},{'accelerators':'tcg\n'}):
            a,b,c,d,e=self.arm(**options)
            with a,b,c,d,e:
                with self.assertRaises(ValueError): cpu.configuration({'QEMU_ACCEL':'kvm'},'qemu')

    def test_real_exec_passes_exact_tcg_argv(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);fake=path/'fake qemu';out=path/'args.json'
            fake.write_text(f'#!{sys.executable}\nimport json,os,sys\nopen(os.environ["ARGV_OUT"],"w").write(json.dumps(sys.argv[1:]))\n')
            fake.chmod(0o755)
            subprocess.run([sys.executable,str(source),'--',str(fake),'-name','ios-kvm'],check=True,
                           env={**os.environ,'QEMU_ACCEL':'tcg','QEMU_TB_MB':'1024','ARGV_OUT':str(out)})
            self.assertEqual(json.loads(out.read_text()),['-cpu','cortex-a76','-accel','tcg,tb-size=1024','-name','ios-kvm'])


if __name__ == '__main__': unittest.main()
