#!/usr/bin/env python3
"""Resolve XNU ustacks serial snapshots using each snapshot's actual cache slide."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def snapshots(text):
    result = []
    current = None
    for line in text.splitlines():
        header = re.search(r'ustacks: (.+)\[(\d+)\] \d+ threads, shared cache slide (0x[0-9a-fA-F]+)', line)
        if header:
            current = {'process': header[1], 'pid': int(header[2]),
                       'slide': int(header[3], 16), 'threads': []}
            result.append(current)
        elif current:
            thread = re.search(r'ustacks:  thread (0x[0-9a-fA-F]+)(?: "([^"]*)")?: (.*)', line)
            if thread:
                current['threads'].append({'id': thread[1], 'name': thread[2] or '',
                    'addresses': [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', thread[3])]})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path, help='session vm.log (default loader ring may omit stacks)')
    parser.add_argument('--cache', required=True, type=Path, help='matching firmware dyld cache')
    parser.add_argument('--ipsw', required=True, type=Path, help='ipsw CLI executable')
    parser.add_argument('--last', type=int, default=1)
    parser.add_argument('--thread', default='', help='thread name substring')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.last < 1:
        parser.error('--last must be positive')
    records = snapshots(args.log.read_text(errors='replace'))[-args.last:]
    if not records:
        parser.error('No ustacks snapshots found in the selected log')
    resolved = {}
    for record in records:
        record['threads'] = [t for t in record['threads'] if args.thread.lower() in t['name'].lower()]
        for thread in record['threads']:
            thread['symbols'] = []
            for address in thread['addresses']:
                key = (record['slide'], address)
                if key not in resolved:
                    if not address:
                        resolved[key] = '<null>'
                    else:
                        answer = subprocess.run([str(args.ipsw.resolve()), 'dyld', 'a2s', str(args.cache.resolve()),
                            hex(address), '--slide', str(record['slide']), '--image'],
                            capture_output=True, text=True, timeout=30)
                        # Unknown/private images remain unknown; never invent attribution.
                        resolved[key] = answer.stdout.strip() if answer.returncode == 0 else '<unresolved>'
                thread['symbols'].append({'address': hex(address), 'symbol': resolved[key]})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'schema': 1, 'cache': str(args.cache.resolve()),
                                      'snapshots': records}, indent=2) + '\n')
    print(args.output)


if __name__ == '__main__':
    main()
