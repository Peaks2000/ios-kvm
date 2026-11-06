#!/usr/bin/env python3
"""Check the real EAGL draw wrappers with instrumented GL calls.

Compile their source with small preparation/GL mocks so skipped draws are
checked independently of a driver's undefined program-zero rasterization.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'vendor/hakios/userland/src/eagl.c').read_text()
enum = re.search(r'enum plain_draw_action \{.*?\n\};', source, re.S)
if enum is None:
    raise SystemExit('Missing EAGL draw-action declaration')
wrappers = []
for name in ('my_glDrawArrays', 'my_glDrawElements'):
    match = re.search(r'static void\n' + name + r'\(.*?\n\}', source, re.S)
    if match is None:
        raise SystemExit('Missing EAGL wrapper ' + name)
    wrappers.append(match[0])
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef unsigned GLenum;
typedef int GLint;
typedef int GLsizei;
struct eagl { unsigned draws, triangles; };
static bool eagl_tracing, trace_placing, trace_listed;
static int skipping;
static bool group, skip;
static unsigned submitted, prepared, cleaned, group_cleaned;
#define EAGL_ENTER(e, slot) ((void)0)
#define EAGL_LEAVE(e, slot) ((void)0)
#define drawing(...) ((void)0)
#define count_spans(...) ((void)0)
#define triangles_of(...) 0
#define eagl_trace(...) ((void)0)
#define trace_draw(...) ((void)0)
#define trace_placed_draw(...) ((void)0)
#define trace_placed_look(...) ((void)0)
#define trace_pixels(...) ((void)0)
#define skip_draw(...) skip
#define group_draw_begin(...) group
#define group_draw_end(...) (++group_cleaned)
'''
fixture += enum[0] + r'''
static enum plain_draw_action action;
static enum plain_draw_action plain_draw_begin(struct eagl *e, GLint enabled[4]) {
    (void)e; (void)enabled;
    prepared++;
    return action;
}
static void plain_draw_end(struct eagl *e, const GLint enabled[4]) {
    (void)e; (void)enabled;
    cleaned++;
}
static void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    assert(mode == 4 && first == 0 && count == 3);
    submitted++;
}
static void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    assert(mode == 4 && count == 3 && type == 0x1403 && indices != NULL);
    submitted++;
}
'''
fixture += '\n'.join(wrappers) + r'''
static void draw_pair(void) {
    struct eagl e = {0};
    const unsigned short indices[] = {0, 1, 2};
    submitted = prepared = cleaned = group_cleaned = 0;
    my_glDrawArrays(&e, 4, 0, 3);
    my_glDrawElements(&e, 4, 3, 0x1403, indices);
    assert(e.draws == 2);
}
int main(void) {
    action = PLAIN_DRAW_SKIP;
    draw_pair();
    assert(submitted == 0 && prepared == 2 && cleaned == 0);
    action = PLAIN_DRAW_NATIVE;
    draw_pair();
    assert(submitted == 2 && prepared == 2 && cleaned == 0);
    action = PLAIN_DRAW_EMULATED;
    draw_pair();
    assert(submitted == 2 && prepared == 2 && cleaned == 2);
    group = true;
    draw_pair();
    assert(submitted == 2 && prepared == 0 && group_cleaned == 2);
    group = false; skipping = 1; skip = true;
    draw_pair();
    assert(submitted == 0 && prepared == 0 && cleaned == 0);
    puts("EAGL draw policy PASSED (arrays and elements, native/emulated/skipped/group draws)");
}
'''
with tempfile.TemporaryDirectory(prefix='eagl-draw-policy-') as directory:
    path = Path(directory)
    (path / 'check.c').write_text(fixture)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(path / 'check.c'), '-o', str(path / 'check')], check=True)
    subprocess.run([str(path / 'check')], check=True)
