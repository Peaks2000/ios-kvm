#!/usr/bin/env python3
"""Switch the running VM's window-server support; keep a reversible baseline."""
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
ssh = ['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes']
saved = root / '.local/exec-inject.original'
disable = '--disable' in sys.argv
if disable:
    config = saved.read_text().strip()
else:
    config = subprocess.check_output(ssh + ['/var/jb/usr/sbin/sysctl -n kern.exec_inject'], text=True).strip()
    if '/var/tmp/ios-kvm-gpu/' not in config:
        saved.write_text(config + '\n')
    else:
        config = saved.read_text().strip()
    entries = []
    for entry in config.split(';'):
        parts = entry.split()
        if not parts:
            continue
        library, *names = parts
        names = [name for name in names if name != 'backboardd']
        if names:
            entries.append(' '.join([library] + names))
    entries.append('/var/tmp/ios-kvm-gpu/libios-kvm-gpu.dylib backboardd')
    config = ';'.join(entries)
command = '/var/jb/usr/sbin/sysctl -w ' + shlex.quote('kern.exec_inject=' + config)
command += ' && /var/jb/usr/bin/killall backboardd'
subprocess.run(ssh + [command], check=True)
