#!/usr/bin/env python3
"""Offline selection/lifecycle checks. No QEMU process is launched by this test suite."""
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import gpu_host

spec = importlib.util.spec_from_file_location('vm_session', Path(__file__).with_name('run-session.py'))
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)

class Selection(unittest.TestCase):
    def test_invalid_mode_is_rejected_before_probing(self):
        with patch.object(gpu_host, 'build_probe') as build:
            with self.assertRaises(ValueError): gpu_host.select_gpu('typo')
            with self.assertRaises(ValueError): gpu_host.select_gpu('software', '/dev/dri/renderD128')
            build.assert_not_called()

    def check_hardware(self, renderer, node):
        devices=[{'index':0,'render_node':node}, {'index':1,'render_node':None}]
        value={'ok':True,'renderer':renderer,'vendor':'fixture','render_node':node,'pixel':[64,128,191,255]}
        with patch.object(gpu_host,'build_probe',return_value=Path('/unused')), \
             patch.object(gpu_host,'probe',side_effect=[(devices,None),(value,None),
                 ({'ok':True,'renderer':'llvmpipe','render_node':None},None)]), \
             patch.object(gpu_host,'probe_vtest',return_value={'ok':True,'caps_bytes':1408}):
            result=gpu_host.select_gpu()
        self.assertTrue(result['selected']['hardware'])
        self.assertEqual(result['selected']['environment']['VTEST_RENDERNODE'],node)

    def test_vendor_neutral_selection(self):
        # These fixtures check policy only; they are not AMD/Intel hardware validation.
        for name,node in [('NVIDIA GeForce','/dev/dri/renderD128'),('AMD Radeon','/dev/dri/renderD129'),
                          ('Mesa Intel Graphics','/dev/dri/renderD130')]:
            with self.subTest(renderer=name): self.check_hardware(name,node)

    def test_failed_hardware_uses_validated_software(self):
        hardware={'ok':True,'renderer':'GPU','render_node':'/dev/dri/renderD128'}
        software={'ok':True,'renderer':'llvmpipe (LLVM test)','render_node':None}
        with patch.object(gpu_host,'build_probe',return_value=Path('/unused')), \
             patch.object(gpu_host,'probe',side_effect=[([{'index':0}],None),(hardware,None),(software,None)]), \
             patch.object(gpu_host,'software_environment',return_value={'LIBGL_ALWAYS_SOFTWARE':'1'}), \
             patch.object(gpu_host,'probe_vtest',side_effect=[{'ok':False,'error':'fixture failure'},{'ok':True}]):
            result=gpu_host.select_gpu()
        self.assertFalse(result['selected']['hardware'])
        self.assertEqual(result['selected']['renderer'],software['renderer'])

    def test_failed_shader_pixels_skip_hardware_and_use_validated_software(self):
        failure={'error':'Textured shader/blend/padded BGRA validation failed'}
        software={'ok':True,'renderer':'llvmpipe (LLVM test)','render_node':None,
                  'shader_pixels':64,'padded_bgra':True}
        with patch.object(gpu_host,'build_probe',return_value=Path('/unused')), \
             patch.object(gpu_host,'probe',side_effect=[([{'index':0}],None),
                 (None,failure),(None,failure),(software,None)]), \
             patch.object(gpu_host,'software_environment',return_value={'LIBGL_ALWAYS_SOFTWARE':'1'}), \
             patch.object(gpu_host,'probe_vtest',return_value={'ok':True}) as vtest:
            result=gpu_host.select_gpu()
        self.assertFalse(result['selected']['hardware'])
        self.assertEqual(len(result['failures']),2)
        vtest.assert_called_once_with(software)

    def test_explicit_unavailable_device_never_silently_falls_back(self):
        with patch.object(gpu_host,'build_probe',return_value=Path('/unused')), \
             patch.object(gpu_host,'probe',return_value=([{'index':0,'render_node':'/dev/dri/renderD128'}],None)) as probe:
            result=gpu_host.select_gpu('auto','/dev/dri/renderD999')
        self.assertIsNone(result['selected'])
        self.assertEqual(probe.call_count,1)

    def test_wrong_software_renderer_is_rejected(self):
        with patch.object(gpu_host,'build_probe',return_value=Path('/unused')), \
             patch.object(gpu_host,'probe',return_value=({'ok':True,'renderer':'Hardware GPU'},None)):
            result=gpu_host.select_gpu('software')
        self.assertIsNone(result['selected'])

class FakeProcess:
    def __init__(self,pid,name,events,fail_helper=False,interrupt=False):
        self.pid,self.name,self.events=pid,name,events
        self.fail_helper,self.interrupt=fail_helper,interrupt
        self.keep_running=fail_helper or interrupt
        self.returncode=None
        self.polls=0
    def poll(self):
        self.polls+=1
        if self.name=='vm' and self.interrupt:
            self.interrupt=False
            raise KeyboardInterrupt
        if self.fail_helper and self.name=='renderer': self.returncode=17
        if self.name=='vm' and not self.keep_running and self.polls>1: self.returncode=0
        return self.returncode
    def wait(self,timeout=None):
        self.events.append(('wait',self.name))
        self.returncode=0 if self.returncode is None else self.returncode
        return self.returncode

class Lifecycle(unittest.TestCase):
    def run_case(self,failed=False,interrupt=False):
        events=[]; children={}
        def popen(arguments,**kwargs):
            name='relay' if 'virgl-relay.py' in arguments[1] else 'renderer' if 'start-gpu-server.py' in arguments[1] else 'vm'
            child=FakeProcess(900000+len(children),name,events,failed,interrupt)
            children[name]=child
            if name=='renderer': self.assertEqual(kwargs['env']['QEMU_INSTANCE'],'0')
            if name=='vm':
                self.assertEqual(kwargs['env']['IOS_KVM_SESSION_MANAGED'],'1')
                self.assertEqual(kwargs['env']['QEMU_SHARED_RAM'],'1')
            return child
        def kill(pid,sig):
            child=next(p for p in children.values() if p.pid==pid)
            events.append(('terminate',child.name))
            child.returncode=-sig
        def sync(*args,**kwargs):
            events.append(('sync','guest'))
            return subprocess.CompletedProcess(args,0)
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(session,'ROOT',Path(directory)), \
             patch.object(session.sys,'argv',['run-session.py','0']), \
             patch.dict(os.environ,{'QEMU_INSTANCE':'0'},clear=True), \
             patch.object(session,'port_free'), \
             patch.object(session,'select_gpu',return_value={'selected':{'renderer':'fixture','hardware':True}}), \
             patch.object(session.subprocess,'Popen',side_effect=popen), \
             patch.object(session.subprocess,'run',side_effect=sync), \
             patch.object(session.os,'killpg',side_effect=kill), \
             patch.object(session.signal,'signal'), patch.object(session.time,'sleep'):
            if failed:
                with self.assertRaisesRegex(RuntimeError,'renderer exited'): session.main()
            else:
                self.assertEqual(session.main(),130 if interrupt else 0)
            state=json.loads((Path(directory)/'.local/vm-session.json').read_text())
        self.assertEqual(state['status'],'failed' if failed else 'stopped')
        self.assertEqual([name for event,name in events if event=='terminate'],['vm','renderer','relay'])
        if failed or interrupt:
            self.assertLess(events.index(('sync','guest')),events.index(('terminate','vm')))

    def test_normal_exit_cleans_every_helper(self): self.run_case()
    def test_unsupported_instance_is_rejected_before_probing(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(session,'ROOT',Path(directory)), \
             patch.object(session.sys,'argv',['run-session.py','2']), \
             patch.object(session,'select_gpu') as select:
            with self.assertRaisesRegex(RuntimeError,'instance 0'): session.main()
            select.assert_not_called()
    def test_failed_renderer_flushes_then_stops_vm_and_helpers(self): self.run_case(failed=True)
    def test_interrupt_flushes_and_cleans_helpers(self): self.run_case(interrupt=True)
    def test_busy_port_is_rejected(self):
        with socket.socket() as listener:
            listener.bind(('127.0.0.1',0)); listener.listen(1)
            with self.assertRaisesRegex(RuntimeError,'already in use'):
                session.port_free(listener.getsockname()[1])

if __name__=='__main__': unittest.main()
