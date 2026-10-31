#!/usr/bin/env python3
"""Add an opt-in timer experiment to the locally patched XNU tree."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / 'vendor/hakios/xnu-src/xnu-8796.121.2/osfmk/arm64/machine_routines.c'
text = path.read_text()
old = '\tcdp->cpu_decrementer = dec_value;'
new = '''\t/* ios-kvm: leave deadlines unchanged unless the explicit runtime experiment is enabled. */
\textern uint32_t ios_kvm_timer_floor_ticks;
\tuint32_t floor = os_atomic_load(&ios_kvm_timer_floor_ticks, relaxed);
\tif (dec_value < floor) {
\t\tdec_value = floor;
\t}
\tcdp->cpu_decrementer = dec_value;'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit('Unexpected ml_set_decrementer source')
    path.write_text(text.replace(old, new, 1))
