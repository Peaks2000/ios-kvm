#!/usr/bin/env python3
"""Reserve the full aligned range of an early ARM framebuffer mapping."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / 'vendor/hakios/xnu-src/xnu-8796.121.2/osfmk/arm/io_map.c'
text = path.read_text()
old = '\t\tvirtual_space_start += round_page(size);'
new = '''\t\t/* Include the physical page offset already accounted for by alloc_size.
\t\t * Otherwise the next early mapping overwrites the final framebuffer page. */
\t\tvirtual_space_start += alloc_size;'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit('Unexpected early ARM io_map allocation')
    path.write_text(text.replace(old, new, 1))
