#!/usr/bin/env python3
"""Describe live-site evidence without promoting page activity to functional proof.

Consumes metrics.json produced by bench-report.py and the associated bench logs.
No network, browser action, symbolization or new frame-rate calculation occurs.
Regenerate bench-report.py first if the underlying logs have changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import urlsplit

TARGETS = {'X': ('x.com', 'twitter.com'), 'New York Times': ('nytimes.com',),
           'The Verge': ('theverge.com',), 'YouTube': ('youtube.com', 'youtu.be')}
TIMESTAMP = re.compile(r'^\s*(\d+\.\d+) (.*)')
LOAD_FAILURE = re.compile(r'\b(?:NetworkResourceLoader|WebResourceLoader|WebFrameLoaderClient|FrameLoader|WebPageProxy)::(?:didFailLoading|didFailLoad|didFailProvisionalLoad|didFailNavigation|didFailProvisionalNavigation)\b|\bTIGER(?: ui)?: (?:load failed|loadDidFail)\b', re.I)
FUNCTIONS = ('login', 'content_navigation', 'forms_and_text_editing', 'semantic_scroll_and_pointer_actions',
             'target_video_identity', 'play_pause_seek', 'audio_output', 'streaming', 'displayed_frame_rate')


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def target_for_url(url):
    host = (urlsplit(url).hostname or '').lower().rstrip('.')
    for name, domains in TARGETS.items():
        if any(host == domain or host.endswith('.' + domain) for domain in domains):
            return name
    return None


def load_diagnostics(lines):
    """Known loader failure logs only; samples containing __error are not failures."""
    failures, cancellations = [], []
    for number, raw in enumerate(lines, 1):
        match = TIMESTAMP.match(raw)
        message = match[2] if match else raw.rstrip()
        if not LOAD_FAILURE.search(message):
            continue
        entry = {'line': number, 'seconds': float(match[1]) if match else None, 'message': message[:400]}
        (cancellations if 'isCancellation=1' in message else failures).append(entry)
    return {'failure_markers': len(failures), 'cancellation_markers': len(cancellations),
            'examples': failures[:12], 'cancellation_examples': cancellations[:3],
            'scope': 'Known log markers only; may be subresources. Main-document success is not established by their absence.'}


def summarize_run(directory, name, metric):
    log = directory / (name + '.log')
    diagnostics = load_diagnostics(log.read_text(errors='replace').splitlines()) if log.is_file() else None
    operations = metric.get('paint_operations', {})
    counts = {key: value.get('count') for key, value in operations.get('operations', {}).items()}
    inputs = metric.get('input', {})
    stage = metric.get('stage_exit_status')
    crashes = metric.get('crashes', [])
    titles = metric.get('titles', [])
    if stage not in (None, 0):
        observation = 'run-failed'
    elif crashes:
        observation = 'crash-markers-observed'
    elif not log.is_file():
        observation = 'log-missing'
    elif (counts.get('ui_draw') or 0) > 0:
        observation = 'window-paint-activity-observed'
    elif any(count or 0 for count in counts.values()):
        observation = 'render-pipeline-activity-observed'
    elif titles or metric.get('fvnl') is not None:
        observation = 'page-metadata-or-layout-observed'
    else:
        observation = 'no-page-render-evidence'
    media = metric.get('media', [])
    paint_file = directory / (name + '.video-paints.json')
    video = {'status': 'not-recorded', 'target_video_identity': 'unverified', 'frame_rate_acceptance': 'unverified'}
    if paint_file.is_file():
        try:
            report = json.loads(paint_file.read_text())
            video.update(status='recorded', measurement=report.get('measurement'),
                         measurement_status=report.get('status'), seconds=report.get('seconds'),
                         expected_pixels=report.get('expected_pixels'), expected_display=report.get('expected_display'),
                         streams=report.get('streams', []), artifact_sha256=sha256(paint_file))
        except (ValueError, TypeError, AttributeError) as error:
            video.update(status='invalid-artifact', error=str(error))
    return {'name': name, 'requested_url': metric['url'], 'observation': observation,
            'functional_acceptance': 'unverified', 'checks': {key: 'unverified' for key in FUNCTIONS},
            'stage_exit_status': stage, 'stage_completion': 'unrecorded' if stage is None else ('success' if stage == 0 else 'failure'),
            'last_title': titles[-1][1] if titles else None, 'titles': titles,
            'first_visually_nonempty_layout_s': metric.get('fvnl'),
            'paint_operations': operations,
            'input_activity': {'logged_moves': inputs.get('tti_moves', 0) + inputs.get('hover_n', 0),
                               'logged_wheels': inputs.get('wheel_n', 0),
                               'cursor_response_pairs': len(inputs.get('hover', [])),
                               'scroll_frame_response_pairs': len(inputs.get('wheel', [])),
                               'first_cursor_response_under_100ms_s': inputs.get('tti'),
                               'scope': 'Scripted event/cursor/scroll-frame activity; no hit-target or semantic action assertion.'},
            'crash_markers': {'count': len(crashes), 'examples': crashes[:12],
                              'scope': 'No marker does not prove crash-free execution.'},
            'load_diagnostics': diagnostics,
            'decoder_telemetry': {'status': 'recorded' if media else 'not-recorded', 'windows': media,
                                  'scope': 'TIGER-MEDIA decoder output; may include ads or other media. Does not prove presentation, audio, or requested-video playback.'},
            'video_window_paints': video,
            'screenshot': {'status': 'captured-unreviewed' if (directory / (name + '.png')).is_file() else 'not-recorded',
                           'path': name + '.png'},
            'artifacts': {'log': name + '.log', 'log_sha256': sha256(log),
                          'video_paints': paint_file.name if paint_file.is_file() else None}}


def build_report(directory, metrics):
    directory = Path(directory)
    targets = {name: {'assessment': 'not-recorded', 'runs': []} for name in TARGETS}
    for name, metric in metrics.items():
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
            continue
        target = target_for_url(metric.get('url', ''))
        if target:
            targets[target]['runs'].append(summarize_run(directory, name, metric))
            targets[target]['assessment'] = 'functional-and-streaming-acceptance-unverified'
    candidate = directory / 'candidate.json'
    return {'schema': 1, 'scope': 'Observation report, not a functional acceptance pass. All four live targets require separate asserted browsing and media tasks.',
            'metrics_sha256': sha256(directory / 'metrics.json'), 'candidate_record_sha256': sha256(candidate),
            'targets': targets}


def markdown(report):
    def cell(value):
        return str(value).replace('|', '\\|').replace('\n', ' ').replace('\r', ' ')
    lines = ['## Live-site acceptance evidence', '',
             'Titles, layout milestones, paint operations and synthetic input responses show activity; they do not establish correct content or functional browsing. Screenshots listed here have not been reviewed.', '',
             '| target / run | requested URL | last title | observed activity | input moves / wheels / scroll responses | crashes / load failures / cancellations | media evidence |',
             '|---|---|---|---|---|---|---|']
    for target, record in report['targets'].items():
        if not record['runs']:
            lines.append('| ' + cell(target) + ' | — | — | not recorded | — | — | not recorded |')
        for run in record['runs']:
            counts = {key: data.get('count') for key, data in run['paint_operations'].get('operations', {}).items()}
            activity = run['observation'] + '; UI draws=' + str(counts.get('ui_draw', 'unrecorded'))
            activity += '; stage=' + run['stage_completion']
            inputs = run['input_activity']
            diagnostics = run['load_diagnostics']
            issues = f"{run['crash_markers']['count']} / {diagnostics['failure_markers']} / {diagnostics['cancellation_markers']}" if diagnostics else 'log missing'
            media = f"decoder windows={len(run['decoder_telemetry']['windows'])}; video paint telemetry={run['video_window_paints']['status']}"
            lines.append('| ' + ' | '.join(cell(value) for value in (target + ' / ' + run['name'], run['requested_url'], run['last_title'] or 'not recorded', activity,
                         f"{inputs['logged_moves']} / {inputs['logged_wheels']} / {inputs['scroll_frame_response_pairs']}", issues, media)) + ' |')
    lines += ['', '**Unverified for every target:** login, content navigation, forms/text editing, semantic pointer/scroll behavior, requested video identity, play/pause/seek, audio output, streaming and displayed frame rate.', '',
              'Decoder FPS is not displayed FPS. Optional video telemetry reports individual ring window paints, never physical scanout; unknown source/display dimensions or unverified media identity prevent frame-rate acceptance. A YouTube title or an unrelated ad stream does not verify the requested video. Resource failures may be cancellations or subresources; zero detected markers is not proof of a clean load. Full observations and diagnostic examples are in `live-sites.json`.']
    return '\n'.join(lines) + '\n'


def write_report(directory, metrics=None):
    directory = Path(directory)
    if metrics is None:
        metrics = json.loads((directory / 'metrics.json').read_text())
    report = build_report(directory, metrics)
    text = markdown(report)
    (directory / 'live-sites.json').write_text(json.dumps(report, indent=2) + '\n')
    (directory / 'live-sites.md').write_text(text)
    return text


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    print(write_report(args.directory), end='')
