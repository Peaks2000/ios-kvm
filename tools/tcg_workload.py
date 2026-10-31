"""Shared deterministic indirect-call workload generator for CPU comparisons."""
from pathlib import Path


def many_targets(directory: Path, count: int) -> Path:
    if count not in (1024, 4096):
        raise ValueError('target count must be 1024 or 4096')
    lines = []
    for i in range(count):
        constant = ((i + 1) * 0x9e3779b97f4a7c15) & ((1 << 64) - 1)
        body = f'uint64_t v=(x^UINT64_C(0x{constant:016x}))*UINT64_C(0xbf58476d1ce4e5b9);'
        if i % 3 == 1:
            body += 'v^=v>>17;'
        elif i % 3 == 2:
            body += 'v=(v<<11)|(v>>53);'
        lines.append(f'__attribute__((noinline)) static uint64_t wide_{i}(uint64_t x)'
                     f'{{{body}return v+UINT64_C({i*2+1});}}')
    lines.append('static work_fn volatile many_steps[]={' + ','.join(f'wide_{i}' for i in range(count)) + '};')
    lines.append('__attribute__((noinline)) static uint64_t dispatch_many(uint64_t x){'
                 f'for(unsigned i=0;i<1000000;i++)x=many_steps[(x>>16)&{count-1}](x);return x;}}')
    generated = directory / 'tcg-many-targets.inc'
    generated.write_text('\n'.join(lines) + '\n')
    return generated
