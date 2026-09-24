#!/usr/bin/env python3
"""Interpret separate staging/gate exits without mislabeling legacy combined exits."""
import json
from pathlib import Path


def read_status(directory, name):
    directory = Path(directory)

    def status(suffix):
        path = directory / (name + suffix)
        return int(path.read_text().strip()) if path.is_file() else None

    overall = status('.exit-status')
    stage = status('.stage-exit-status')
    gate = status('.gate-exit-status')
    stage_source = 'explicit' if stage is not None else 'unrecorded'
    gate_source = 'explicit' if gate is not None else 'unrecorded'
    if stage is None and overall is not None:
        # Old zero exits imply both stages succeeded. A nonzero could instead be
        # an FPS gate failure, so it cannot be attributed to stage-app.sh.
        stage = 0 if overall == 0 else None
        stage_source = 'legacy-combined-success' if overall == 0 else 'legacy-combined-unknown'
    if gate is None:
        report = directory / (name + '.video-paints.json')
        if report.is_file():
            try:
                passed = json.loads(report.read_text()).get('benchmark_gate', {}).get('passed')
                if type(passed) is bool:
                    gate = 0 if passed else 1
                    gate_source = 'video-paint-report'
            except (ValueError, AttributeError, TypeError):
                pass
    return {'exit_status': overall, 'stage_exit_status': stage, 'stage_status_source': stage_source,
            'benchmark_gate_exit_status': gate, 'benchmark_gate_status_source': gate_source}
