#!/usr/bin/env python3
"""Validate real host async readbacks and subsequent wire ordering without a VM."""
import argparse
import ctypes
import fcntl
import json
import mmap
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time
from virgl_draw import build_draw_encoder, expected_pixel, pixels_match

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--server',type=Path,required=True)
p.add_argument('--profile',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.parent.mkdir(parents=True,exist_ok=True)
fd=os.memfd_create('ios-kvm-ram',os.MFD_CLOEXEC|os.MFD_ALLOW_SEALING)
os.fchmod(fd,0o600);os.ftruncate(fd,1<<20)
fcntl.fcntl(fd,fcntl.F_ADD_SEALS,fcntl.F_SEAL_GROW|fcntl.F_SEAL_SHRINK|fcntl.F_SEAL_SEAL)
ram=mmap.mmap(fd,1<<20)
load=ctypes.CDLL('libatomic.so.1').__atomic_load_4
load.argtypes=[ctypes.c_void_p,ctypes.c_int];load.restype=ctypes.c_uint32
ram_address=ctypes.addressof(ctypes.c_char.from_buffer(ram))
env=dict(os.environ);profile=json.loads(a.profile.read_text());env.update(profile['selected']['environment'])
env.update(IOS_KVM_RAM_FILE=f'/proc/{os.getpid()}/fd/{fd}',IOS_KVM_RAM_SIZE=str(1<<20))
def read(s,n):
    out=bytearray()
    while len(out)<n:
        data=s.recv(n-len(out))
        if not data:raise RuntimeError('Host disconnected')
        out.extend(data)
    return bytes(out)
def send(s,command,data=b''):
    s.sendall(struct.pack('<II',len(data)//4,command)+data)
def connect(path):
    s=socket.socket(socket.AF_UNIX);s.settimeout(10);s.connect(path)
    name=b'ios-kvm-async-host-check\0';s.sendall(struct.pack('<II',len(name),8)+name)
    send(s,9);length,kind=struct.unpack('<II',read(s,8))
    if kind!=2 or not 1<length<=65537:raise RuntimeError('Invalid caps')
    read(s,length-1)
    return s
def query(s):
    send(s,7,struct.pack('<II',0,0x494b0000))
    if struct.unpack('<II',read(s,8))!=(1,7):raise RuntimeError('Async socket reply corrupted the next command')
    return struct.unpack('<I',read(s,4))[0]
def request(handle,seq,completion=0x40010000,segments=None):
    segments=segments or [(0x40002000,128),(0x40004000,128)]
    args=[handle,0,32,0,0,0,0,8,8,1,256,len(segments)]
    return struct.pack('<12I',*args)+b''.join(struct.pack('<QQ',*s) for s in segments)+struct.pack('<QI',completion,seq)
def complete(seq):
    deadline=time.monotonic()+10
    while load(ram_address+0x10004,2)!=seq:
        if time.monotonic()>deadline:raise RuntimeError('No completion published')
        time.sleep(.001)
    return struct.unpack_from('<i',ram,0x10000)[0]
process=None;results=[]
try:
    encoder=build_draw_encoder()
    with tempfile.TemporaryDirectory(prefix='ios-kvm-async-') as temp, a.output.with_suffix('.log').open('w') as log:
        path=str(Path(temp)/'socket')
        process=subprocess.Popen([str(a.server.resolve()),'--use-egl-surfaceless','--socket-path',path],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        deadline=time.monotonic()+15
        while not Path(path).exists():
            if process.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Host did not start')
            time.sleep(.05)
        with connect(path) as s:
            if query(s)!=0x494b0107:raise RuntimeError('Missing async capability')
            send(s,2,struct.pack('<10I',1,2,67,10,8,8,1,1,0,0))
            send(s,2,struct.pack('<10I',2,0,64,16,96,1,1,1,0,0))
            for frame in range(20):
                ram[0x2000:0x2080]=b'\xcc'*128;ram[0x4000:0x4080]=b'\xcc'*128
                commands=subprocess.check_output([str(encoder),str(frame), '8','8'],timeout=5)
                send(s,6,commands);send(s,66,request(1,frame+1))
                status=complete(frame+1)
                actual=ram[0x2000:0x2080]+ram[0x4000:0x4080]
                if status or not pixels_match(actual,expected_pixel(frame)):raise RuntimeError(f'Async pixel mismatch frame {frame}, status {status}')
                if query(s)!=0x494b0107:raise RuntimeError('Feature reply changed')
            send(s,66,request(999,21));status=complete(21)
            if not status or query(s)!=0x494b0107:raise RuntimeError('Resource error did not complete cleanly')
            for sequence,segments in [(22,[(0x40002000,255)]),(23,[(0x40100000,256)]),(24,[(0x40002000,0)]),(25,[(0xffffffffffffffff,256)])]:
                send(s,66,request(1,sequence,segments=segments));status=complete(sequence)
                if not status or query(s)!=0x494b0107:raise RuntimeError('Bad destination did not publish error without corrupting stream')
                results.append({'invalid_segments':segments,'error_status':status})
            results.append({'frames':20,'pixels':1280,'segmented':True,'error_status':status,'next_command_reply_intact':True})
        for label,body in [('unaligned',request(1,22,0x40010001)),('outside',request(1,22,0x40100000)),('overlap',request(1,22,0x40002000)),('second-overlap',request(1,22,0x40004000)),('zero-cell',request(1,22,0)),('zero-sequence',request(1,0)),('bad-length',request(1,22)+b'\0'*4)]:
            with connect(path) as s:
                ram[0x10000:0x10008]=struct.pack('<iI',123,25)
                send(s,66,body)
                try:data=s.recv(4)
                except ConnectionResetError:data=b''
                if data or ram[0x10000:0x10008]!=struct.pack('<iI',123,25):raise RuntimeError(label+': malformed command was accepted or wrote completion')
                results.append({'rejected':label})
    a.output.write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results))
finally:
    if process is not None:
        try:os.killpg(process.pid,signal.SIGTERM)
        except ProcessLookupError:pass
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
    ram.close();os.close(fd)
