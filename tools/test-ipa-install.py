#!/usr/bin/env python3
"""IPA validation and SSH installer contract tests. No VM changes."""
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import importlib.util
import types
import ipa_install as installer


class Installer(unittest.TestCase):
    def make_ipa(self, directory, info=None, filename='My app;$(touch nope).ipa'):
        path=Path(directory)/filename
        with zipfile.ZipFile(path,'w') as archive:
            archive.writestr('Payload/Test.app/Info.plist',plistlib.dumps(
                info if info is not None else {'CFBundleIdentifier':'dev.ioskvm.test','CFBundleDisplayName':'Test App'}))
        return path

    def test_reads_bounded_app_metadata_without_extracting(self):
        with tempfile.TemporaryDirectory() as directory:
            path=self.make_ipa(directory)
            self.assertEqual(installer.inspect_ipa(path),(path,'Test App','dev.ioskvm.test'))
            self.assertEqual(len(list(Path(directory).iterdir())),1)

    def test_invalid_archives_and_missing_identifier(self):
        with tempfile.TemporaryDirectory() as directory:
            bad=Path(directory)/'bad.ipa';bad.write_bytes(b'not a zip')
            for path in (bad,self.make_ipa(directory,{})):
                with self.assertRaises(installer.InstallError):installer.inspect_ipa(path)

    def test_multiple_apps_and_oversized_metadata_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path=self.make_ipa(directory)
            with zipfile.ZipFile(path,'a') as archive:
                archive.writestr('Payload/Other.app/Info.plist',b'{}')
            with self.assertRaises(installer.InstallError):installer.inspect_ipa(path)
            with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as archive:
                archive.writestr('Payload/Test.app/Info.plist',b'x'*(1024*1024+1))
            with self.assertRaises(installer.InstallError):installer.inspect_ipa(path)

    def test_success_streams_file_and_never_embeds_local_filename_in_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            path=self.make_ipa(directory);messages=[]
            with patch.object(installer.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'','')) as run:
                self.assertEqual(installer.install(path,messages.append),'dev.ioskvm.test')
                self.assertEqual(run.call_count,4)
                for call in run.call_args_list:self.assertNotIn(path.name,call.args[0][-1])
                self.assertIn('install /var/tmp/ios-kvm-install-',run.call_args_list[2].args[0][-1])
                self.assertIn('rm -f /var/tmp/ios-kvm-install-',run.call_args_list[3].args[0][-1])
                self.assertEqual(run.call_args_list[1].kwargs['stdin'].name,str(path))
            self.assertTrue(messages[-1].startswith('Installed Test App'))

    def test_missing_helper_does_not_upload_or_attempt_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(installer.subprocess,'run',return_value=subprocess.CompletedProcess([],127,'','')) as run:
                with self.assertRaises(installer.InstallError):installer.install(self.make_ipa(directory),lambda _:None)
                self.assertEqual(run.call_count,1)

    def test_install_timeout_cleans_uploaded_file(self):
        with tempfile.TemporaryDirectory() as directory:
            ok=subprocess.CompletedProcess([],0,'','')
            with patch.object(installer.subprocess,'run',side_effect=[ok,ok,subprocess.TimeoutExpired('install',600),ok]) as run:
                with self.assertRaises(installer.InstallError):installer.install(self.make_ipa(directory),lambda _:None)
                self.assertIn('rm -f',run.call_args_list[-1].args[0][-1])


class DropHandler(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            spec=importlib.util.spec_from_file_location('ipa_drop',Path(__file__).with_name('ipa-drop.py'))
            cls.gui=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.gui)
        except ImportError:
            raise unittest.SkipTest('GTK/PyGObject not installed')

    def drop(self, uris, busy=False):
        paths=[];messages=[];finished=[]
        window=types.SimpleNamespace(busy=busy,begin=paths.append,
                                     message=types.SimpleNamespace(set_text=messages.append))
        selection=types.SimpleNamespace(get_uris=lambda:uris)
        with patch.object(self.gui.Gtk,'drag_finish',side_effect=lambda *args:finished.append(args)):
            self.gui.Window.drop(window,None,'context',0,0,selection,0,123)
        return paths,messages,finished

    def test_file_manager_uri_is_decoded_and_accepted(self):
        paths,_,finished=self.drop(['file:///tmp/My%20App.ipa'])
        self.assertEqual(paths,['/tmp/My App.ipa'])
        self.assertEqual(finished,[('context',True,False,123)])

    def test_remote_empty_and_multiple_drops_are_rejected(self):
        for uris in (None,[],['https://example.invalid/app.ipa'],['file://server/app.ipa'],
                     ['file:///one.ipa','file:///two.ipa']):
            paths,messages,finished=self.drop(uris)
            self.assertEqual(paths,[]);self.assertTrue(messages)
            self.assertFalse(finished[0][1])

    def test_busy_drop_does_not_start_second_install(self):
        paths,_,finished=self.drop(['file:///tmp/app.ipa'],busy=True)
        self.assertEqual(paths,[]);self.assertFalse(finished[0][1])


if __name__ == '__main__':unittest.main()
