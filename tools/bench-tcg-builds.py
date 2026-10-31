#!/usr/bin/env python3
"""Compare AArch64 user-mode emulator builds with fixed workloads. Never boots a VM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('candidate',type=Path)
parser.add_argument('--baseline',default=shutil.which('qemu-aarch64'))
parser.add_argument('--cpu',type=int,help='Pin only the benchmark children to one allowed host CPU')
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
if args.cpu is not None and args.cpu not in os.sched_getaffinity(0):
    parser.error('--cpu is not in the allowed host affinity')
if not args.baseline:
    parser.error('qemu-aarch64 is missing; specify --baseline')
executables={'baseline':Path(args.baseline).resolve(),'candidate':args.candidate.resolve()}
versions={}
for label,path in executables.items():
    version=subprocess.check_output([str(path),'--version'],text=True,timeout=5)
    if not version.startswith('qemu-aarch64 version '):
        parser.error(f'{path} is not the AArch64 user-mode emulator; system emulators are rejected')
    versions[label]=version.splitlines()[0]
args.output.parent.mkdir(parents=True,exist_ok=True)
logs=args.output.with_suffix('.logs');logs.mkdir(exist_ok=True)
source=ROOT/'tools/tcg-cpu-bench.c'
pattern=re.compile(r'^tcg-cpu: workload=(\w+) sample=(\d+) elapsed_us=(\d+) checksum=(\d+)$',re.M)
expected=None
def run(command,label):
    pin=(lambda:os.sched_setaffinity(0,{args.cpu})) if args.cpu is not None else None
    result=subprocess.run(command,check=True,text=True,capture_output=True,timeout=180,preexec_fn=pin)
    (logs/f'{label}.log').write_text(result.stdout+result.stderr)
    records=[(name,int(sample),int(elapsed),int(checksum)) for name,sample,elapsed,checksum in pattern.findall(result.stdout)]
    if len(records)!=24 or {name for name,_,_,_ in records}!={'dependent','indirect','memory'}:
        raise RuntimeError(f'Incomplete benchmark output: {label}')
    checksums={}
    for name in ('dependent','indirect','memory'):
        rows=[row for row in records if row[0]==name]
        if sorted(row[1] for row in rows)!=list(range(8)) or len({row[3] for row in rows})!=1:
            raise RuntimeError(f'Invalid samples/checksums: {label}/{name}')
        checksums[name]=rows[0][3]
    if expected is not None and checksums!=expected:
        raise RuntimeError(f'Guest calculation differs from native reference: {label}')
    return records,checksums

with tempfile.TemporaryDirectory(prefix='tcg-benchmark-',dir=args.output.parent) as directory:
    native=Path(directory)/'native';arm=Path(directory)/'arm64'
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(native)],check=True)
    subprocess.run(['clang','--target=aarch64-linux-gnu','-O2','-Wall','-Wextra','-Werror',
                    '-ffreestanding','-fno-stack-protector','-nostdlib','-static','-fuse-ld=lld',
                    '-DIOS_KVM_FREESTANDING',str(source),'-Wl,-e,_start','-o',str(arm)],check=True)
    _,expected=run([str(native)],'native-reference')
    phases=[];values={name:{} for name in executables}
    for number,label in enumerate(('baseline','candidate','candidate','baseline')):
        print(f'Phase {number}: {label} (user-mode calculation, no VM)',flush=True)
        records,_=run([str(executables[label]),'-cpu','cortex-a76',str(arm)],f'phase-{number}-{label}')
        medians={}
        for name in expected:
            samples=[elapsed for work,sample,elapsed,_ in records if work==name and sample>=2]
            values[label].setdefault(name,[]).extend(samples)
            medians[name]=statistics.median(samples)
        phases.append({'phase':number,'build':label,'median_us':medians})
    summary={}
    for name in expected:
        baseline=statistics.median(values['baseline'][name]);candidate=statistics.median(values['candidate'][name])
        summary[name]={'baseline_us':baseline,'candidate_us':candidate,'speed_ratio':round(baseline/candidate,4)}
    receipt={'schema':1,'kind':'AArch64 user-mode calculations; no VM','cpu':'cortex-a76','host_cpu':args.cpu,
             'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'checksums':expected,
             'emulators':{name:{'path':str(path),'version':versions[name],
                 'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for name,path in executables.items()},
             'phases':phases,'summary':summary}
    args.output.write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(summary,indent=2))
