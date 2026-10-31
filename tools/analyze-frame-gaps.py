#!/usr/bin/env python3
"""Report all display-frame gaps in gesture windows, including gaps over 100 ms.

Unlike the upstream frames.py movement summary, this diagnostic retains long
gaps. Windows extend to the next gesture or 2.5 seconds after its start; pauses
after an animation finishes may therefore be included. It is not a sustained
application FPS benchmark.
"""
import argparse
import json
from pathlib import Path
import statistics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('logs', nargs='+', type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--active-touches-only', action='store_true',
                    help='Restrict each window to finger-down through finger-up; omit button/idle windows')
args = parser.parse_args()
seen, gestures, frames, releases, inputs = set(), [], {}, [], []
for path in args.logs:
    for line in path.read_text(errors='replace').splitlines():
        at = line.find('tl: ')
        if at < 0:
            continue
        line = line[at:].strip()
        if line in seen:
            continue
        seen.add(line)
        words = line.split()
        if len(words) < 2:
            continue
        try:
            if words[1] == 'p' and len(words) >= 8 and int(words[2]) in (1, 4):
                gestures.append({'start_us': int(words[7]), 'type': int(words[2]),
                                 'finger': int(words[3]), 'x': int(words[4]), 'y': int(words[5])})
            elif words[1] == 'p' and len(words) >= 8 and int(words[2]) == 3:
                releases.append((int(words[7]), int(words[3])))
            elif words[1] == 'v' and len(words) >= 9:
                frames[int(words[2])] = int(words[8])
            elif words[1] == 'i' and len(words) >= 9:
                inputs.append(tuple(int(words[j]) for j in (2, 4, 6, 8)))
        except ValueError:
            continue
gestures.sort(key=lambda g: g['start_us'])
times = sorted(frames.values())
reports = []
for i, gesture in enumerate(gestures):
    start = gesture['start_us']
    if args.active_touches_only:
        if gesture['type'] != 1:
            continue
        ends = [t for t, finger in releases if t > start and finger == gesture['finger']]
        if not ends:
            continue
        end = min(ends)
    else:
        end = min(start + 2500000, gestures[i+1]['start_us'] if i+1 < len(gestures) else start + 2500000)
    selected = [t for t in times if start <= t < end]
    gaps = sorted((b-a)/1000 for a, b in zip(selected, selected[1:]))
    report = {**gesture, 'frames': len(selected)}
    delivered = [(read-posted)/1000 for _, posted, read, sent in inputs if start <= posted <= end]
    if delivered:
        delivered.sort()
        report.update(delivered_input_events=len(delivered), input_queue_median_ms=statistics.median(delivered),
                      input_queue_p95_ms=delivered[min(len(delivered)-1, (95*len(delivered)+99)//100-1)],
                      input_queue_max_ms=max(delivered))
    if args.active_touches_only:
        bounded = [start, *selected, end]
        report.update(duration_ms=(end-start)/1000,
                      display_updates_per_second=len(selected)*1000000/(end-start),
                      max_gap_with_boundaries_ms=max(b-a for a, b in zip(bounded, bounded[1:]))/1000)
    if gaps:
        report.update(median_gap_ms=statistics.median(gaps),
                      p95_gap_ms=gaps[min(len(gaps)-1, (95*len(gaps)+99)//100-1)],
                      max_gap_ms=max(gaps), gaps_over_100ms=sum(t >= 100 for t in gaps),
                      gaps_over_34_5ms=sum(t > 34.5 for t in gaps), gaps=len(gaps))
    reports.append(report)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps({'schema': 1, 'window_us': None if args.active_touches_only else 2500000,
                                  'includes_idle_after_animation': not args.active_touches_only,
                                  'gestures': reports}, indent=2) + '\n')
for i, report in enumerate(reports, 1):
    print(i, json.dumps(report))
