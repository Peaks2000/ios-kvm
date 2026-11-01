#!/usr/bin/env python3
"""Exercise opt-in surface completion/lifetime rules in an already running private VM."""
import argparse
import fcntl
import json
import mmap
import os
import platform
from pathlib import Path
import select
import socket
import stat
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--modes',nargs='+',choices=['normal','error','exit','timeout','quota','byte-quota'],default=['normal','error','exit','timeout','quota','byte-quota'])
args=parser.parse_args()
args.output.parent.mkdir(parents=True,exist_ok=True)
ssh=['bash',str(ROOT/'tools/ssh.sh'),'-o','BatchMode=yes','-o','ConnectTimeout=15']
def guest(command):
    return subprocess.check_output(ssh+[command],text=True,timeout=30)
def counts():
    state=json.loads(guest('/var/tmp/surface-fence-check state'))
    if not state['enabled']:raise RuntimeError('Requires candidate kernel with gpu_async=1')
    return tuple(state['counts'])
if platform.machine() not in ('x86_64','AMD64'):
    raise RuntimeError('The ordered-store smoke harness currently requires an x86 host')
with socket.create_connection(('127.0.0.1',4445),timeout=10) as sock:
    with sock.makefile('rw') as channel:
        channel.readline()
        def qmp(command,arguments=None):
            channel.write(json.dumps({'execute':command,'arguments':arguments or {}})+'\n');channel.flush()
            while True:
                reply=json.loads(channel.readline())
                if 'error' in reply:raise RuntimeError(reply['error'])
                if 'return' in reply:return reply['return']
        qmp('qmp_capabilities')
        if qmp('query-name').get('name')!='ios-kvm':raise RuntimeError('Wrong QMP machine')
        if not qmp('qom-get',{'path':'/objects/ram','property':'share'}):raise RuntimeError('RAM is not shared')
        path=qmp('qom-get',{'path':'/objects/ram','property':'mem-path'})
        size=qmp('qom-get',{'path':'/objects/ram','property':'size'})
fd=os.open(path,os.O_RDWR|os.O_CLOEXEC)
info=os.fstat(fd)
if (not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode)!=0o600 or info.st_uid!=os.getuid() or info.st_size!=size
    or os.readlink(path)!='/memfd:ios-kvm-ram (deleted)'):
    os.close(fd);raise RuntimeError('Expected this session\'s private RAM memfd')
seals=fcntl.fcntl(fd,fcntl.F_GET_SEALS)
required=fcntl.F_SEAL_GROW|fcntl.F_SEAL_SHRINK|fcntl.F_SEAL_SEAL
if seals&required!=required:os.close(fd);raise RuntimeError('RAM seals missing')
ram=mmap.mmap(fd,size);os.close(fd)
helper=ROOT/'.local/async-readback/surface-fence-check'
helper.parent.mkdir(parents=True,exist_ok=True)
subprocess.run(['clang','-target','arm64-apple-ios16.0','-isysroot',str(ROOT/'.local/sdks/iPhoneOS16.5.sdk'),
    '-fuse-ld=lld','-O2','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',
    '-framework','CoreFoundation','-framework','IOSurface','-framework','IOKit',
    str(ROOT/'tools/surface-fence-check.c'),'-o',str(helper)],check=True)
with helper.open('rb') as source:
    subprocess.run(ssh+['cat > /var/tmp/surface-fence-check && chmod 755 /var/tmp/surface-fence-check'],stdin=source,check=True,timeout=60)
records=[]
process=None
try:
    baseline=counts()
    if baseline[:2]!=(0,0):raise RuntimeError('Other pending work would confound the test')
    for mode in args.modes:
        process=subprocess.Popen(ssh+['/var/tmp/surface-fence-check '+mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
        lines=[];prepared=None;deadline=time.monotonic()+40
        while time.monotonic()<deadline:
            if not select.select([process.stdout],[],[],.2)[0]:continue
            line=process.stdout.readline()
            if not line:break
            lines.append(line)
            if line.startswith('{'):
                event=json.loads(line)
                if event.get('event')=='prepared':prepared=event;break
        if prepared is None:
            process.kill();stdout,stderr=process.communicate();raise RuntimeError('No prepared fence: '+''.join(lines)+stdout+stderr)
        (args.output.parent/(mode+'-prepared.json')).write_text(json.dumps(prepared,indent=2)+'\n')
        before=counts()
        expected=len(prepared.get('fences',[prepared]))
        if before[0]!=expected or before[1]<prepared['bytes']:raise RuntimeError('Kernel did not retain pending backing')
        fences=prepared.get('fences',[prepared])
        for fence in fences:
            for key in ['physical','completion']:
                value=fence[key]
                if value<0x40000000 or value-0x40000000>size-8 or value&3:raise RuntimeError('Bad test address')
            if abs(fence['physical']-fence['completion'])<8:raise RuntimeError('Completion aliases pixels')
        if mode in ['exit','timeout','quota','byte-quota']:
            stdout,stderr=process.communicate(timeout=20);lines.append(stdout)
            if process.returncode:raise RuntimeError('Helper failed before completion: '+''.join(lines)+stderr)
            retained=counts()
            if retained[:2]!=before[:2]:raise RuntimeError('Backing was released after submitting task exited')
        else:
            process.stdin.write('1');process.stdin.flush()
            if not select.select([process.stdout],[],[],20)[0]:raise RuntimeError('Helper did not begin waiting')
            line=process.stdout.readline();lines.append(line)
            if json.loads(line).get('event')!='waiting':raise RuntimeError('Missing wait handshake')
            time.sleep(.05)
        # This harness runs on x86 Linux: ordered aligned stores publish status
        # before sequence, after pixels. Production host code must use release.
        for fence in fences:
            at=fence['physical']-0x40000000
            ram[at:at+4]=struct.pack('<I',0x12345678)
            at=fence['completion']-0x40000000
            ram[at:at+4]=struct.pack('<i',-5 if mode=='error' else 0)
            ram[at+4:at+8]=struct.pack('<I',fence['sequence'])
        if mode not in ['exit','timeout','quota','byte-quota']:
            stdout,stderr=process.communicate(timeout=20);lines.append(stdout)
            if process.returncode:raise RuntimeError('Completed helper failed: '+''.join(lines)+stderr)
        deadline=time.monotonic()+10
        while True:
            after=counts()
            if after[0]==0 and after[1]==0 and after[2]==baseline[2]+expected:break
            if time.monotonic()>deadline:raise RuntimeError('Completed backing was not released: '+str(after))
            time.sleep(.05)
        baseline=after
        (args.output.parent/(mode+'.log')).write_text(''.join(lines)+stderr)
        record={'mode':mode,'prepared':prepared,'pending_before_ack':before,'after_ack':after};records.append(record)
        args.output.write_text(json.dumps(records,indent=2)+'\n');print(json.dumps({"mode":mode,"acknowledged":expected,"pending_before_ack":before,"after_ack":after}),flush=True)
finally:
    if process is not None and process.poll() is None:
        process.kill()
        process.communicate(timeout=10)
    # Failed, unacknowledged requests stay safely retained by the kernel. The
    # prepared receipts permit investigation; shutting down the VM frees them.
    ram.close()
