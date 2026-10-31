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
args = parser.parse_args()
seen, gestures, frames = set(), [], {}
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
        try:
            if words[1] == 'p' and len(words) >= 8 and int(words[2]) in (1, 4):
                gestures.append({'start_us': int(words[7]), 'type': int(words[2]),
                                 'x': int(words[4]), 'y': int(words[5])})
            elif words[1] == 'v' and len(words) >= 9:
                frames[int(words[2])] = int(words[8])
        except ValueError:
            continue
gestures.sort(key=lambda g: g['start_us'])
times = sorted(frames.values())
reports = []
for i, gesture in enumerate(gestures):
    start = gesture['start_us']
    end = min(start + 2500000, gestures[i+1]['start_us'] if i+1 < len(gestures) else start + 2500000)
    selected = [t for t in times if start <= t < end]
    gaps = sorted((b-a)/1000 for a, b in zip(selected, selected[1:]))
    report = {**gesture, 'frames': len(selected)}
    if gaps:
        report.update(median_gap_ms=statistics.median(gaps),
                      p95_gap_ms=gaps[min(len(gaps)-1, (95*len(gaps)+99)//100-1)],
                      max_gap_ms=max(gaps), gaps_over_100ms=sum(t >= 100 for t in gaps),
                      gaps_over_34_5ms=sum(t > 34.5 for t in gaps), gaps=len(gaps))
    reports.append(report)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps({'schema': 1, 'window_us': 2500000,
                                  'includes_idle_after_animation': True,
                                  'gestures': reports}, indent=2) + '\n')
for i, report in enumerate(reports, 1):
    print(i, json.dumps(report))
