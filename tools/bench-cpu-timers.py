#!/usr/bin/env python3
"""Compare opt-in timer settings in one live VM; restore normal deadlines on exit."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
script = ['set -e', "trap '/var/jb/usr/sbin/sysctl -w kern.ios_kvm_timer_floor_us=0' EXIT"]
for phase, interval in enumerate((0, 4000, 0, 4000)):
    script += [f'/var/jb/usr/sbin/sysctl -w kern.ios_kvm_timer_floor_us={interval}',
               f'printf "CPU_PHASE {phase} FLOOR_US {interval}\\n"',
               '/var/tmp/ios-kvm-gpu/cpu-bench']
subprocess.run(['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=30',
                '/var/jb/bin/sh -s'], input='\n'.join(script) + '\n', text=True, check=True)
