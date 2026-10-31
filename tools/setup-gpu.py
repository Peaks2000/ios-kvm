#!/usr/bin/env python3
"""Prepare pinned GPU sources and build the host renderer and iOS bridge. Never boots a VM."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MESA_VERSION = '25.0.7'
MESA_SHA256 = '592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6'
VIRGL_COMMIT = 'e0a1caf3b147bb26dfe524dbb85ded1177e7e49e'
VIRGL_URL = 'https://gitlab.freedesktop.org/virgl/virglrenderer.git'


def run(*arguments, **kwargs):
    return subprocess.run(arguments, cwd=ROOT, check=True, **kwargs)


def prepare_sources():
    local = ROOT / '.local'
    local.mkdir(exist_ok=True)
    mesa = local / f'mesa-{MESA_VERSION}'
    archive = local / f'mesa-{MESA_VERSION}.tar.xz'
    if not mesa.exists():
        if not archive.exists():
            temporary = archive.with_suffix('.download')
            try:
                run('curl', '-fL', '--retry', '3', f'https://archive.mesa3d.org/mesa-{MESA_VERSION}.tar.xz',
                    '-o', str(temporary))
                if hashlib.sha256(temporary.read_bytes()).hexdigest() != MESA_SHA256:
                    raise RuntimeError('Mesa archive checksum mismatch')
                temporary.replace(archive)
            finally:
                temporary.unlink(missing_ok=True)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != MESA_SHA256:
            raise RuntimeError('Cached Mesa archive checksum mismatch; source was not extracted')
        with tempfile.TemporaryDirectory(prefix='mesa-source-', dir=local) as directory:
            with tarfile.open(archive) as bundle:
                bundle.extractall(directory, filter='data')
            (Path(directory) / mesa.name).rename(mesa)
    if not (mesa / 'meson.build').is_file():
        raise RuntimeError(f'Incomplete Mesa source tree: {mesa}')
    virgl = local / 'virglrenderer'
    if not virgl.exists():
        with tempfile.TemporaryDirectory(prefix='virgl-source-', dir=local) as directory:
            staging = Path(directory) / 'source'
            run('git', 'init', str(staging))
            run('git', '-C', str(staging), 'fetch', '--depth', '1', VIRGL_URL, VIRGL_COMMIT)
            run('git', '-C', str(staging), 'checkout', '--detach', 'FETCH_HEAD')
            staging.rename(virgl)
    revision = subprocess.check_output(['git', '-C', str(virgl), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != VIRGL_COMMIT:
        raise RuntimeError(f'Existing virglrenderer is {revision}; expected {VIRGL_COMMIT}. No files reset.')
    run('python3', 'tools/prepare-virgl-mesa.py')
    run('python3', 'tools/prepare-shared-gpu.py')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-only', action='store_true', help='Prepare sources without compiling')
    parser.add_argument('--check-deps', action='store_true', help='Check build dependencies without downloads or builds')
    parser.add_argument('--jobs', type=int, default=int(os.environ.get('BUILD_JOBS', '2')))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    required = ['git', 'curl', 'patch']
    if not args.prepare_only or args.check_deps:
        required += ['cc', 'clang', 'clang++', 'llvm-ar', 'llvm-strip', 'llvm-nm', 'meson', 'ninja', 'pkg-config']
    missing = [tool for tool in required if not shutil.which(tool)]
    if missing:
        raise RuntimeError('Missing GPU build tools: ' + ', '.join(missing))
    if not args.prepare_only or args.check_deps:
        run('pkg-config', '--exists', 'egl', 'gl', 'epoxy', 'gbm')
        run('python3', '-c', 'import mako, yaml, packaging')
    if args.check_deps:
        print('GPU build dependencies available. No downloads or builds performed.')
        return
    if not (ROOT / '.local/sdks/iPhoneOS16.5.sdk/usr/include/stdio.h').is_file():
        raise RuntimeError('Run setup.sh with your own IPSW first to prepare the iOS SDK')
    prepare_sources()
    if args.prepare_only:
        return
    build = ROOT / '.local/virgl-build'
    if not (build / 'build.ninja').exists():
        run('meson', 'setup', str(build), str(ROOT / '.local/virglrenderer'), '--wrap-mode=nofallback',
            '--buildtype=release', '-Dplatforms=egl', '-Dvenus=false', '-Ddrm-renderers=[]',
            '-Dtests=false', '-Dvideo=false')
    run('ninja', '-C', str(build), '-j', str(args.jobs), 'vtest/virgl_test_server')
    run('bash', 'tools/build-virgl.sh', env={**os.environ, 'BUILD_JOBS': str(args.jobs)})
    run('python3', 'tools/build-gpu-support.py')
    run('python3', 'tools/gpu_host.py')
    print('GPU components built and host renderer validated. No VM started.')
    print('Bundle on the next image build: IOS_KVM_BUNDLE_GPU=1 IOS_KVM_GPU_BOOT=1 bash tools/build.sh')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'GPU setup failed: {error}')
