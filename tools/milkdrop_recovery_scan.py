"""Read-only recovery triage. Shader compilation is not a visual playback test."""
from __future__ import annotations

import argparse
import concurrent.futures
from collections import Counter, defaultdict
import csv
import datetime
import hashlib
import html
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[1]
MAX_PRESET_BYTES = 2 * 1024 * 1024
MAX_CODE_BYTES = 32768  # state.h MAX_BIGSTRING_LEN; ReadCode reserves terminators.
# Keep these renderer limits in sync with vis_milk2/md_defines.h.
MAX_CUSTOM_WAVES = 16
MAX_CUSTOM_SHAPES = 16
LABELS = {
    'live_test_candidate': 'Candidate for live testing',
    'legacy_candidate': 'Legacy preset: live testing needed',
    'visual_review': 'Drawing / visual formula review needed',
    'metadata_review': 'Metadata or numbered-code review needed',
    'shader_repair': 'Shader repair needed',
    'empty': 'Empty: replacement original needed',
    'missing': 'File not found',
    'ambiguous': 'Multiple files share this name',
    'unreadable': 'Could not read file',
    'incomplete': 'Assessment incomplete',
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normal_name(value: str) -> str:
    return value.strip().replace('\\', '/').rsplit('/', 1)[-1].casefold()


def read_names(path: Path) -> dict[str, str]:
    names = {}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if not normal_name(line).endswith('.milk'):
            raise ValueError(f'Expected one .milk filename per line: {path}: {line}')
        names.setdefault(normal_name(line), line.replace('\\', '/').rsplit('/', 1)[-1])
    return names


def integer(values: dict, key: str, default: int) -> int:
    match = re.match(r'\s*([+-]?\d+)', values.get(key, ''))
    result = int(match[1]) if match else default
    if not -(2**31) <= result < 2**31:
        raise ValueError(f'{key} exceeds the importer signed integer range.')
    return result


def numeric(values: dict, key: str, default: float = 0) -> float:
    try:
        return float(values.get(key, default))
    except ValueError:
        return default


def stage_version(values: dict, stage: str) -> int:
    version = integer(values, 'MILKDROP_PRESET_VERSION', 100)
    if version < 200:
        return 0
    if version == 200:
        return integer(values, 'PSVERSION', 2)
    return integer(values, 'PSVERSION_' + stage.upper(), 2)


def parse_preset(data: bytes) -> dict:
    flags, notes = [], []
    if data.startswith((b'\xff\xfe', b'\xfe\xff')) or b'\0' in data:
        return {'values': {}, 'stages': {}, 'flags': ['encoding'],
                'notes': ['UTF-16 or embedded NUL bytes require an encoding review; no shader assessment attempted.']}
    if data.startswith(b'\xef\xbb\xbf'):
        flags.append('encoding')
        notes.append('UTF-8 BOM precedes the first key. The production importer does not remove it; review shader activation metadata.')
    text = data.decode('latin1')
    if re.search(r'^\[PRESET[12]_BEGIN\]\s*$', text, re.MULTILINE):
        return {'values': {}, 'stages': {}, 'flags': ['embedded_composition'],
                'notes': ['MilkDrop3 composition container, including containers named .milk: individual numbered shader streams must not be combined or counted as a complete native preset. Composition rendering is not implemented.']}
    values, duplicate = {}, set()
    for line in re.split(r'[\r\n]+', text):
        # Match _GetLineByName: case-sensitive keys, one space or '=' delimiter,
        # no trimming. A tab is part of the key; BOM bytes are not removed.
        match = re.match(r'^([^ =]*)(?: |=)(.*)$', line)
        if not match:
            continue
        key = match[1]
        if not key or key.startswith(('#', ';', '//')):
            continue  # No production importer lookup requests these keys.
        if len(key) > 124:
            flags.append('long_key')
            continue
        if key in values:
            duplicate.add(key)
        values.setdefault(key, match[2])
    if duplicate:
        # Rating affects preset selection, not its rendered output.
        if duplicate - {'fRating'}:
            flags.append('duplicate_keys')
            notes.append('Duplicate keys need review; the importer lookup cursor can affect which value is used: ' + ', '.join(sorted(duplicate)[:8]))
        else:
            notes.append('Duplicate fRating affects the selection rating only; it does not block a visual test.')
    if 'long_key' in flags:
        notes.append('An overlong key can be truncated by the importer; manual review required.')
    if '[preset00]' not in text.casefold():
        # Production reads keys without requiring a section; absence is not a parse failure.
        notes.append('No [preset00] header; this can be a drawing fragment. The production importer still reads its keys.')
    stages = {}
    for stage in ('warp', 'comp'):
        lines = {int(match[1]): value for key, value in values.items()
                 if (match := re.fullmatch(stage + r'_([1-9]\d*)', key))}
        if not lines:
            continue
        parts, index, length = [], 1, 0
        while index in lines:
            line = lines[index][:MAX_CODE_BYTES - 4]
            if len(line) < len(lines[index]):
                flags.append('code_limit')
                notes.append(f'{stage}: numbered line {index} exceeds the importer line buffer and is truncated.')
            if line == '~!@#$' or len(line) >= MAX_CODE_BYTES - 2 - length:
                flags.append('code_limit')
                notes.append(f'{stage}: code stops at line {index} because of the importer sentinel or buffer limit.')
                break
            part = line.removeprefix('`') + '\1'
            parts.append(part)
            length += len(part)
            index += 1
        if index not in lines and any(number >= index for number in lines):
            flags.append('code_gap')
            notes.append(f'{stage}: missing numbered line {index}; later code is ignored by the importer.')
        raw = ''.join(parts).encode('latin1')
        version = stage_version(values, stage)
        stages[stage] = {'raw': raw, 'declared_version': version,
                         'profile': 'ps_5_0' if version >= 5 else 'ps_4_0'}
        if version <= 0 and raw.replace(b'\1', b'').strip():
            flags.append('inactive_shader_metadata')
            notes.append(f'{stage}: shader code exists but preset metadata disables it. Compilation below is a hypothetical repair check.')
        clean = re.sub(r'/\*.*?\*/|//[^\x01\r\n]*', '', raw.decode('latin1'), flags=re.S)
        if re.search(r'\bret\s*-=\s*ret\s*;|\bret\s*=\s*ret\s*-\s*ret\s*;', clean):
            flags.append('zero_output_expression')
            notes.append(f'{stage}: a self-subtraction zeros ret; check the surrounding equations and intended effect.')
    active_custom = any(integer(values, f'{kind}code_{n}_enabled', 0) != 0
                        for kind, count in (('wave', MAX_CUSTOM_WAVES), ('shape', MAX_CUSTOM_SHAPES))
                        for n in range(count))
    dynamic_equations = any(re.match(r'(?:per_frame|per_pixel|wave_\d+|shape_\d+)_', k) for k in values)
    main_wave = numeric(values, 'fWaveAlpha', 0.8) > 0
    border = any(numeric(values, p + '_size') > 0 and numeric(values, p + '_a') > 0 for p in ('ib', 'ob'))
    vectors = numeric(values, 'mv_a') > 0
    if not stages and not (active_custom or main_wave or border or vectors):
        flags.append('no_static_drawing_source')
        notes.append('No enabled static drawing source found. Per-frame equations may change visibility; this is a review hint, not a confirmed blank result.' if dynamic_equations else 'No enabled static drawing source or dynamic equations found.')
    return {'values': values, 'stages': stages, 'flags': sorted(set(flags)), 'notes': notes}


def index_presets(directory: Path, wanted: set[str]) -> dict[str, list[Path]]:
    found = defaultdict(list)
    def walk_error(error):
        raise error
    for base, dirs, files in os.walk(directory, onerror=walk_error, followlinks=False):
        dirs[:] = [name for name in dirs if not (Path(base)/name).is_symlink()]
        for name in files:
            if name.casefold() not in wanted:
                continue
            path = Path(base)/name
            if path.is_symlink() or not path.resolve().is_relative_to(directory):
                continue
            found[name.casefold()].append(path)
    return found


def historical_candidates() -> dict[str, str]:
    """Use old audit evidence only when the exact preset bytes still match."""
    audit = REPO/'TestResults/hidden-presets-audit-2026-10-05.json'
    table = REPO/'dist/dx11-blank-review-2026-10-05/hidden-preset-results.csv'
    if not audit.is_file() or not table.is_file():
        return {}
    try:
        hashes = {normal_name(p['name']): p.get('sha256') for p in json.loads(audit.read_text())['presets']}
        result = {}
        with table.open(encoding='utf-8-sig', newline='') as stream:
            for row in csv.DictReader(stream):
                if row['staged_repair'].lower() == 'true':
                    continue
                new = row['new_compile']
                if new and all(part.endswith(':ok') for part in new.split(';')) and ('failed' in row['old_compile'] or row['old_density_guard'].lower() == 'true'):
                    result[normal_name(row['preset'])] = hashes.get(normal_name(row['preset']))
        return result
    except (OSError, ValueError, KeyError):
        return {}


def compile_stage(compiler: Path, include: Path, output: Path, stage: str, details: dict, timeout: int) -> dict:
    raw = output/f'{stage}.raw'
    raw.write_bytes(details['raw'])
    binary = output/f'{stage}.cso'
    started = time.monotonic()
    try:
        proc = subprocess.run([str(compiler), str(include), str(raw), '0' if stage == 'warp' else '1',
                               details['profile'], str(binary)], capture_output=True, timeout=timeout,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        diagnostic = (proc.stdout + proc.stderr).decode('utf-8', errors='replace').replace('\0', '')
        status = 'passed' if proc.returncode == 0 and binary.is_file() and binary.stat().st_size else ('failed' if proc.returncode in (1, 3) else 'tool_error')
        result = {'status': status, 'returncode': proc.returncode, 'diagnostic': diagnostic}
    except subprocess.TimeoutExpired:
        result = {'status': 'timeout', 'diagnostic': f'Compiler stopped after {timeout} seconds; recovery status remains unknown.'}
    except OSError as error:
        result = {'status': 'tool_error', 'diagnostic': f'Could not run compiler: {error}'}
    (output/f'{stage}.log').write_text(result['diagnostic'], encoding='utf-8')
    return {**result, 'profile': details['profile'], 'declared_version': details['declared_version'],
            'seconds': round(time.monotonic()-started, 3), 'log': str(output/f'{stage}.log')}


def assess(name: str, paths: list[Path], output: Path, compiler: Path, include: Path, timeout: int, old: dict) -> dict:
    row = {'name': name, 'paths': [str(p) for p in paths], 'status': 'incomplete', 'priority': 'Normal',
           'bytes': None, 'sha256': None, 'notes': [], 'flags': [], 'shaders': {}}
    if len(paths) != 1:
        row['status'] = 'missing' if not paths else 'ambiguous'
        return row
    path = paths[0]
    try:
        size = path.stat().st_size
        row['bytes'] = size
        if size > MAX_PRESET_BYTES:
            row['notes'] = [f'File exceeds the {MAX_PRESET_BYTES}-byte diagnostic limit; no assessment attempted.']
            return row
        data = path.read_bytes()
        row.update(bytes=len(data), sha256=sha(data))
        if not data:
            row['status'] = 'empty'
            return row
        parsed = parse_preset(data)
        row.update(notes=parsed['notes'], flags=parsed['flags'])
        output.mkdir()
        for stage, details in parsed['stages'].items():
            if details['raw']:
                row['shaders'][stage] = compile_stage(compiler, include, output, stage, details, timeout)
        statuses = {s['status'] for s in row['shaders'].values()}
        if statuses & {'timeout', 'tool_error'}:
            row['status'] = 'incomplete'
        elif 'failed' in statuses:
            row['status'] = 'shader_repair'
        elif set(parsed['flags']) & {'encoding', 'duplicate_keys', 'code_gap', 'code_limit', 'inactive_shader_metadata', 'long_key', 'embedded_composition'}:
            row['status'] = 'metadata_review'
        elif set(parsed['flags']) & {'zero_output_expression', 'no_static_drawing_source'}:
            row['status'] = 'visual_review'
        elif row['shaders']:
            row['status'] = 'live_test_candidate'
        else:
            row['status'] = 'legacy_candidate'
        if row['status'] == 'live_test_candidate' and old.get(normal_name(name)) == row['sha256']:
            row['priority'] = 'High'
            row['notes'].append('Exact bytes match a historical compiler/profile restriction that the updated DX11 path removes. Still requires live testing.')
        if sha(path.read_bytes()) != row['sha256']:
            row['status'] = 'incomplete'
            row['notes'].append('Preset changed while being scanned; rerun before using this result.')
    except OSError as error:
        row['status'] = 'unreadable'
        row['notes'].append(str(error))
    except ValueError as error:
        row['status'] = 'metadata_review'
        row['notes'].append(f'Malformed preset data prevented a reliable assessment: {error}')
    return row


def write_reports(output: Path, rows: list[dict], context: dict) -> dict:
    rows = sorted(rows, key=lambda r: (r['priority'] != 'High', r['status'], r['name'].casefold()))
    counts = dict(Counter(r['status'] for r in rows))
    report = {**context, 'counts': counts, 'assessed_names': len(rows), 'presets': rows}
    (output/'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    with (output/'results.csv').open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['Preset', 'Assessment', 'Priority', 'Bytes', 'SHA256', 'Warp', 'Composite', 'Notes', 'Paths'])
        for row in rows:
            cells = [row['name'], LABELS[row['status']], row['priority'], row['bytes'], row['sha256'],
                             row['shaders'].get('warp', {}).get('status', ''), row['shaders'].get('comp', {}).get('status', ''),
                             ' | '.join(row['notes']), ' | '.join(row['paths'])]
            # Text from user files must not become an Excel formula on opening.
            writer.writerow(["'" + v if isinstance(v, str) and v.lstrip().startswith(('=', '+', '-', '@')) else v for v in cells])
    queue = [r for r in rows if r['status'] in ('live_test_candidate', 'legacy_candidate')]
    (output/'live-test-candidates.txt').write_text('# Candidates only. Compilation does not prove visible output.\n' +
                                                ''.join(r['name']+'\n' for r in queue), encoding='utf-8')
    e = html.escape
    body = ['<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">',
            '<title>MilkDrop recovery assessment</title><style>body{font:16px system-ui;margin:24px;background:#161b22;color:#edf2f7}table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:10px;border-bottom:1px solid #404850;vertical-align:top}th{background:#252d37}p{max-width:1100px;line-height:1.5}details{max-width:900px}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}input{padding:10px;width:320px;max-width:90%;margin:15px 0}small{color:#bec8d5}</style>',
            '<h1>MilkDrop recovery assessment</h1>',
            '<p>'+e(context['limits'])+'</p>',
            '<p>Created '+e(context['created'])+'. Scope: '+e(context['scope'])+'. Names assessed: '+str(len(rows))+'.</p>',
            '<ul>'+''.join('<li>'+e(LABELS[k])+': <b>'+str(v)+'</b></li>' for k,v in sorted(counts.items()))+'</ul>',
            '<p>High priority means matching earlier compiler-failure evidence plus successful current compilation. No preset is labelled recovered by this scan.</p>',
            '<input id="filter" aria-label="Filter results" placeholder="Filter by name, status or note"><table><thead><tr><th>Preset</th><th>Assessment</th><th>Priority</th><th>Evidence</th></tr></thead><tbody>']
    if context.get('input_warning'):
        body.insert(-1, '<p><strong>'+e(context['input_warning'])+'</strong></p>')
    for row in rows:
        evidence = '<br>'.join(e(note) for note in row['notes'])
        evidence += '<br><small>'+e(' | '.join(row['paths']))+'</small>'
        for stage, result in row['shaders'].items():
            evidence += '<details><summary>'+e(stage+' '+result['profile']+': '+result['status'])+'</summary><pre>'+e(result['diagnostic'] or 'Compilation completed without a diagnostic.')+'</pre></details>'
        body.append('<tr><td>'+e(row['name'])+'</td><td>'+e(LABELS[row['status']])+'</td><td>'+e(row['priority'])+'</td><td>'+evidence+'</td></tr>')
    body += ['</tbody></table><script>document.getElementById("filter").addEventListener("input",function(){const q=this.value.toLowerCase();document.querySelectorAll("tbody tr").forEach(r=>r.hidden=!r.textContent.toLowerCase().includes(q));});</script></html>']
    (output/'report.html').write_text('\n'.join(body), encoding='utf-8')
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--milkdrop-root', type=Path, required=True)
    parser.add_argument('--preset-list', type=Path)
    parser.add_argument('--output-directory', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--include-file', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=15)
    parser.add_argument('--workers', type=int, default=2)
    args = parser.parse_args()
    root = args.milkdrop_root.resolve(strict=True)
    presets = (root/'presets').resolve(strict=True)
    output = args.output_directory.resolve()
    # Diagnostic artefacts must never be written into the live foobar profile.
    if output.is_relative_to(root.parent) or root.parent.is_relative_to(output):
        raise ValueError('Choose an output directory outside the foobar2000 profile.')
    if not 1 <= args.workers <= 4 or not 1 <= args.timeout <= 120:
        raise ValueError('Workers must be 1–4 and timeout 1–120 seconds.')
    if output.exists():
        raise ValueError('Output directory already exists; choose a new path to preserve earlier reports.')
    listing = (args.preset_list or root/'preset-blacklist.txt').resolve(strict=True)
    names = read_names(listing)
    if not names:
        raise ValueError('The selected preset list is empty.')
    compiler = args.compiler.resolve(strict=True)
    include = args.include_file.resolve(strict=True)
    context = {'created': datetime.datetime.now().astimezone().isoformat(), 'scope': str(listing),
        'list_sha256': sha(listing.read_bytes()), 'scanner_sha256': sha(Path(__file__).read_bytes()),
        'milkdrop_root': str(root), 'compiler': str(compiler), 'compiler_sha256': sha(compiler.read_bytes()),
        'include_file': str(include), 'include_sha256': sha(include.read_bytes()),
        'limits': 'Read-only preliminary assessment using this checkout\'s DX11 shader preparation. Presets, settings and exclusions are untouched. No live rendering, textures, EEL execution, sound response or visual fidelity is tested. Compilation success is only a candidate for live testing. Shader Model 4 assumes feature level 10 or later; Shader Model 5 requires feature level 11.',
        'completed': False}
    output.mkdir(parents=True)
    work = output/'shader-checks'
    work.mkdir()
    print(f'Indexing preset filenames for {len(names)} requested names...', flush=True)
    found = index_presets(presets, set(names))
    history = historical_candidates()
    rows = []
    started = time.monotonic()
    print(f'Compiling with {args.workers} workers; {args.timeout}s maximum per shader.', flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = [pool.submit(assess, name, found.get(key, []), work/f'{number:04d}', compiler, include, args.timeout, history)
                   for number, (key, name) in enumerate(sorted(names.items()))]
        for future in concurrent.futures.as_completed(futures):
            row = future.result()
            rows.append(row)
            with (output/'progress.jsonl').open('a', encoding='utf-8') as stream:
                stream.write(json.dumps(row)+'\n')
            if len(rows) % 20 == 0 or len(rows) == len(names):
                print(f'{len(rows)}/{len(names)} assessed ({time.monotonic()-started:.0f}s)', flush=True)
    context.update(completed=True, seconds=round(time.monotonic()-started, 1))
    if (sha(include.read_bytes()) != context['include_sha256'] or
            sha(listing.read_bytes()) != context['list_sha256'] or
            sha(compiler.read_bytes()) != context['compiler_sha256']):
        context['input_warning'] = 'An input list, shader include or compiler changed during the scan; rerun before relying on these results.'
    report = write_reports(output, rows, context)
    print('\nAssessment complete:')
    for status, count in sorted(report['counts'].items()):
        print(f'  {LABELS[status]}: {count}')
    print(f'\nReport: {output / "report.html"}')
    print(f'Test shortlist: {output / "live-test-candidates.txt"}')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f'Assessment could not complete: {error}', file=sys.stderr)
        raise SystemExit(2)
