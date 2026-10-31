#!/usr/bin/env python3
"""Select an unbenchmarked development-kernel experiment; retain bootstrap policy.

Fast requires a fresh BUILD directory and make overrides UBSAN_CHECKS= UBSAN_RUNTIME=.
Changing the source options alone does not disable compiler instrumentation. Preserve
the working kernel and restore its BUILD directory/profile after candidate builds.
This helper is deliberately not invoked by normal setup or kernel builds.
"""
from pathlib import Path
import re
import sys

profile = sys.argv[1] if len(sys.argv) == 2 else ''
if profile not in ('baseline', 'fast'):
    raise SystemExit('usage: prepare-kernel-profile.py baseline|fast')
root = Path(__file__).resolve().parents[1]
path = root / 'vendor/hakios/xnu-src/xnu-8796.121.2/config/MASTER.arm64.bcm2837'
groups = {
    # Retain assertions until DEVELOPMENT-only callers are disentangled from them.
    'KERNEL_DEV': ('KERNEL_BASE development mach_assert config_xnupost proc_ref_debug', 'KERNEL_BASE development mach_assert config_xnupost'),
    # Keep the IOKit tracking ABI used by the staged kernel SDK headers.
    'IOKIT_DEV': ('IOKIT_BASE iokitstats iotracking', 'IOKIT_BASE iokitstats iotracking'),
    'LIBKERN_DEV': ('LIBKERN_BASE iotracking', 'LIBKERN_BASE iotracking'),
    # DTrace provides thread_get_kernel_stack to DEVELOPMENT task-reference tracing;
    # the interactive debugger also provides functions used by IOKit/reboot paths.
    'PERF_DBG_DEV': ('PERF_DBG_BASE config_dtrace zleaks kdp_interactive_debugging alternate_debugger config_iotrace', 'PERF_DBG_BASE config_dtrace zleaks kdp_interactive_debugging alternate_debugger config_iotrace'),
    'MACH_DEV': ('MACH_BASE config_io_accounting importance_trace', 'MACH_BASE config_skip_precise_user_kernel_time'),
    'VM_DEV': ('VM_BASE dynamic_codesigning ubsan_minimal pgzalloc', 'VM_BASE dynamic_codesigning'),
}
text = path.read_text()
for name, options in groups.items():
    pattern = re.compile(r'(^#\s+' + name + r'\s*=\s*\[)([^\]]+)(\])', re.M)
    found = list(pattern.finditer(text))
    legacy = {'KERNEL_DEV': ('KERNEL_BASE development', 'KERNEL_BASE development mach_assert'),
              'PERF_DBG_DEV': ('PERF_DBG_BASE', 'PERF_DBG_BASE zleaks'),
              'IOKIT_DEV': ('IOKIT_BASE',), 'LIBKERN_DEV': ('LIBKERN_BASE',)}
    allowed = options + legacy.get(name, ())
    if len(found) != 1 or ' '.join(found[0][2].split()) not in allowed:
        raise SystemExit(f'Unexpected kernel group {name}; no profile written')
    chosen = options[profile == 'fast']
    text = pattern.sub(lambda m: m[1] + ' ' + chosen + ' ' + m[3], text)
if path.read_text() != text:
    path.write_text(text)
print(f'Kernel profile: {profile}; DEVELOPMENT/bootstrap/signing configuration retained')
if profile == 'fast':
    print('Experimental only: use fresh BUILD and make UBSAN_CHECKS= UBSAN_RUNTIME=; preserve the working kernel')
