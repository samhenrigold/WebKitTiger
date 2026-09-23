#!/usr/bin/env python3
"""Summarize timestamped paint probes; these are not distinct-video-frame counts."""
import argparse
import json
import re


TIMESTAMP = re.compile(r'^\s*(\d+\.\d+) (.*)')
PROBES = {
    'gpu_render': re.compile(r'TIGER gpu: frame \d.*? render ([\d.]+) ms'),
    'gpu_readback': re.compile(r'TIGER gpu: frame \d.*? readback ([\d.]+) ms'),
    'ui_incorporate': re.compile(r'TIGER ui: incorporate ([\d.]+) ms'),
    'ui_draw': re.compile(r'TIGER ui: drawRect ([\d.]+) ms'),
}


def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    return values[round((len(values) - 1) * fraction)]


def summarize(lines, start=4.0, end=None):
    samples = {name: [] for name in PROBES}
    last = None
    for line in lines:
        match = TIMESTAMP.match(line)
        if not match:
            continue
        time, message = float(match[1]), match[2]
        last = max(last or time, time)
        if time < start or (end is not None and time >= end):
            continue
        for name, pattern in PROBES.items():
            if found := pattern.search(message):
                samples[name].append((time, float(found[1])))
    stop = min(end, last) if end is not None and last is not None else last
    duration = max(0, stop - start) if stop is not None else 0
    result = {'start_s': start, 'end_s': stop, 'duration_s': duration,
              'meaning': 'Completed probe operations, not distinct video frames or physical scanout.', 'operations': {}}
    for name, values in samples.items():
        times = [time for time, _ in values]
        costs = [cost for _, cost in values]
        gaps = [(b - a) * 1000 for a, b in zip(times, times[1:])]
        result['operations'][name] = {
            'count': len(values), 'per_second': len(values) / duration if duration else None,
            'cost_p50_ms': percentile(costs, 0.5), 'cost_p95_ms': percentile(costs, 0.95),
            'gap_p50_ms': percentile(gaps, 0.5), 'gap_p95_ms': percentile(gaps, 0.95),
            'gap_max_ms': max(gaps) if gaps else None,
        }
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    parser.add_argument('--start', type=float, default=4.0)
    parser.add_argument('--end', type=float)
    args = parser.parse_args()
    if args.start < 0 or (args.end is not None and args.end <= args.start):
        parser.error('require 0 <= start < end')
    with open(args.log, errors='replace') as stream:
        print(json.dumps(summarize(stream, args.start, args.end), indent=2))
