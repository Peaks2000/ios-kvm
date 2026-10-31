#!/usr/bin/env python3
"""Compile checks of QEMU's actual jump-cache hash/page-invalidation layout; no VM."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
header=root/'.local/qemu-10.2.2/accel/tcg/tb-hash.h'
text=header.read_text()
soft=text.split('#ifdef CONFIG_SOFTMMU\n',1)[1].split('\n#else\n',1)[0]
user=text.split('\n#else\n',1)[1].split('\n#endif',1)[0]
prefix='''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint64_t vaddr;
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)
'''
checks=r'''
int main(void) {
    const uint64_t addresses[]={0,0x40000000ULL,0x100000000ULL,
        0x180123456ULL,0xfffffff000000000ULL,UINT64_MAX};
    for(unsigned page=0;page<sizeof(addresses)/sizeof(addresses[0]);page++) {
        uint64_t size=UINT64_C(1)<<TARGET_PAGE_BITS;
        uint64_t base=addresses[page]&~(size-1);
        for(uint64_t offset=0;offset<size;offset++) {
            unsigned index=tb_jmp_cache_hash_func(base+offset);
            assert(index<TB_JMP_CACHE_SIZE);
#ifdef CHECK_SOFTMMU
            unsigned first=tb_jmp_cache_hash_page(base);
            assert(first+TB_JMP_PAGE_SIZE<=TB_JMP_CACHE_SIZE);
            assert(index>=first && index<first+TB_JMP_PAGE_SIZE);
#endif
        }
    }
    printf("cache layout passed: bits=%d page_bits=%d\n",TB_JMP_CACHE_BITS,TARGET_PAGE_BITS);
    return 0;
}
'''
(root/'.local').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='tcg-layout-',dir=root/'.local') as directory:
    path=Path(directory)
    for mode,code in [('softmmu',soft),('user',user)]:
        source=path/f'{mode}.c';source.write_text(prefix+code+checks)
        for bits in (12,16):
            for page_bits in (10,12,14,16):
                binary=path/'test'
                flags=['-DCHECK_SOFTMMU'] if mode=='softmmu' else []
                subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror',f'-DTB_JMP_CACHE_BITS={bits}',
                                f'-DTARGET_PAGE_BITS={page_bits}',*flags,str(source),'-o',str(binary)],check=True)
                subprocess.run([str(binary)],check=True)
