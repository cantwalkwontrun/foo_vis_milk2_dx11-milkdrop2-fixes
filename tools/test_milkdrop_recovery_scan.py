"""Regression tests for read-only recovery triage; no live profile or UI access."""
from __future__ import annotations

import contextlib
import csv
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import milkdrop_recovery_scan as scan


HEADER = b'MILKDROP_PRESET_VERSION=201\nPSVERSION_WARP=2\nPSVERSION_COMP=2\n'
WARP = b'warp_1=`shader_body { ret = GetPixel(uv); }\n'


class PresetImportTests(unittest.TestCase):
    def test_metadata_versions_follow_import_not_shared_header_guess(self):
        cases = [
            (b'PSVERSION=5\n', 0, 0),
            (b'MILKDROP_PRESET_VERSION=199\nPSVERSION=5\n', 0, 0),
            (b'MILKDROP_PRESET_VERSION=200\nPSVERSION=4\nPSVERSION_WARP=0\nPSVERSION_COMP=5\n', 4, 4),
            (b'MILKDROP_PRESET_VERSION=200\n', 2, 2),
            (b'MILKDROP_PRESET_VERSION=201\nPSVERSION=5\n', 2, 2),
            (b'MILKDROP_PRESET_VERSION=201\nPSVERSION_WARP=0\nPSVERSION_COMP=5\n', 0, 5),
            (b'MILKDROP_PRESET_VERSION=202\nPSVERSION_WARP=4\nPSVERSION_COMP=3\n', 4, 3),
            (b'MILKDROP_PRESET_VERSION=not-a-number\nPSVERSION=5\n', 0, 0),
            (b'MILKDROP_PRESET_VERSION=201 trailing text\nPSVERSION_WARP=  +4junk\nPSVERSION_COMP=no\n', 4, 2),
        ]
        for header, warp, comp in cases:
            with self.subTest(header=header):
                parsed = scan.parse_preset(header + WARP + b'comp_1=`shader_body { ret = 1; }\n')
                self.assertEqual(parsed['stages']['warp']['declared_version'], warp)
                self.assertEqual(parsed['stages']['comp']['declared_version'], comp)

    def test_native_profile_and_disabled_metadata(self):
        for version, profile, inactive in [(-1, 'ps_4_0', True), (0, 'ps_4_0', True), (1, 'ps_4_0', False), (3, 'ps_4_0', False), (4, 'ps_4_0', False), (5, 'ps_5_0', False), (8, 'ps_5_0', False)]:
            with self.subTest(version=version):
                parsed = scan.parse_preset(f'MILKDROP_PRESET_VERSION=201\nPSVERSION_WARP={version}\n'.encode() + WARP)
                self.assertEqual(parsed['stages']['warp']['profile'], profile)
                self.assertEqual('inactive_shader_metadata' in parsed['flags'], inactive)

    def test_section_header_is_not_required_by_import(self):
        parsed = scan.parse_preset(HEADER + WARP)
        self.assertEqual(parsed['stages']['warp']['declared_version'], 2)
        self.assertNotIn('inactive_shader_metadata', parsed['flags'])
        self.assertNotIn('encoding', parsed['flags'])

    def test_space_delimited_metadata_is_supported_by_import(self):
        parsed = scan.parse_preset(b'MILKDROP_PRESET_VERSION 201\nPSVERSION_WARP 4\n' + WARP)
        self.assertEqual(parsed['stages']['warp']['declared_version'], 4)
        self.assertNotIn('inactive_shader_metadata', parsed['flags'])

    def test_missing_code_line_truncates_instead_of_joining_later_code(self):
        parsed = scan.parse_preset(HEADER + b'warp_1=`shader_body {\nwarp_3=`ret = 1; }\n')
        self.assertIn('code_gap', parsed['flags'])
        self.assertEqual(parsed['stages']['warp']['raw'], b'shader_body {\1')

    def test_blank_numbered_line_does_not_truncate(self):
        parsed = scan.parse_preset(HEADER + b'warp_1=`shader_body {\nwarp_2=\nwarp_3=`ret = 1; }\n')
        self.assertNotIn('code_gap', parsed['flags'])
        self.assertEqual(parsed['stages']['warp']['raw'], b'shader_body {\1\1ret = 1; }\1')

    def test_code_without_line_one_is_not_compiled_as_complete(self):
        parsed = scan.parse_preset(HEADER + b'warp_2=`shader_body { ret = 1; }\n')
        self.assertIn('code_gap', parsed['flags'])
        self.assertEqual(parsed['stages']['warp']['raw'], b'')

    def test_absent_shader_stage_can_use_generated_default(self):
        parsed = scan.parse_preset(HEADER + b'[preset00]\nfWaveAlpha=1\n')
        self.assertFalse(parsed['stages'])
        self.assertNotIn('code_gap', parsed['flags'])

    def test_duplicate_keys_are_reported(self):
        parsed = scan.parse_preset(HEADER + b'PSVERSION_WARP=5\n' + WARP)
        self.assertIn('duplicate_keys', parsed['flags'])

    def test_embedded_composition_is_not_compiled_as_one_native_preset(self):
        source = b'[PRESET1_BEGIN]\r\n' + HEADER + WARP + b'[PRESET1_END]\r\n[PRESET2_BEGIN]\r\n' + HEADER + WARP
        parsed = scan.parse_preset(source)
        self.assertEqual(parsed['flags'], ['embedded_composition'])
        self.assertEqual(parsed['stages'], {})
        ordinary = scan.parse_preset(HEADER + WARP + b'; [PRESET1_BEGIN]\n')
        self.assertNotIn('embedded_composition', ordinary['flags'])

    def test_ignored_empty_keys_do_not_create_duplicate_metadata_failures(self):
        parsed = scan.parse_preset(HEADER + b' indented unused text\n another unused line\n=orphan value\n' + WARP)
        self.assertNotIn('', parsed['values'])
        self.assertNotIn('duplicate_keys', parsed['flags'])
        self.assertEqual(parsed['stages']['warp']['raw'], b'shader_body { ret = GetPixel(uv); }\1')

    def test_duplicate_rating_does_not_hide_a_real_duplicate_setting(self):
        parsed = scan.parse_preset(HEADER + b'fRating=1\nfRating=2\nfWaveAlpha=0\nfWaveAlpha=1\n' + WARP)
        self.assertIn('duplicate_keys', parsed['flags'])

    def test_single_code_line_obeys_production_read_buffer_boundary(self):
        # GetFastString(32768) passes32767 to a reader reserving three bytes.
        for length, truncated in [(32764, False), (32765, True), (40000, True)]:
            with self.subTest(length=length):
                parsed = scan.parse_preset(HEADER + b'warp_1=' + b'a' * length + b'\n')
                self.assertEqual(parsed['stages']['warp']['raw'], b'a' * 32764 + b'\1')
                self.assertEqual('code_limit' in parsed['flags'], truncated)

    def test_aggregate_shader_buffer_accepts_last_fitting_line_only(self):
        parsed = scan.parse_preset(HEADER + b'warp_1=' + b'a' * 32763 + b'\nwarp_2=b\nwarp_3=c\n')
        self.assertEqual(parsed['stages']['warp']['raw'], b'a' * 32763 + b'\1b\1')
        self.assertEqual(len(parsed['stages']['warp']['raw']), 32766)
        self.assertIn('code_limit', parsed['flags'])
        self.assertNotIn('code_gap', parsed['flags'])

    def test_line_equal_to_remaining_code_limit_is_rejected(self):
        parsed = scan.parse_preset(HEADER + b'warp_1=' + b'a' * 32763 + b'\nwarp_2=bc\n')
        self.assertEqual(parsed['stages']['warp']['raw'], b'a' * 32763 + b'\1')
        self.assertIn('code_limit', parsed['flags'])

    def test_importer_sentinel_is_checked_before_removing_backtick(self):
        for value, stopped in [(b'~!@#$', True), (b'`~!@#$', False)]:
            with self.subTest(value=value):
                parsed = scan.parse_preset(HEADER + b'warp_1=start\nwarp_2=' + value + b'\nwarp_3=end\n')
                expected = b'start\1' if stopped else b'start\1~!@#$\1end\1'
                self.assertEqual(parsed['stages']['warp']['raw'], expected)
                self.assertEqual('code_limit' in parsed['flags'], stopped)
                self.assertNotIn('code_gap', parsed['flags'])

    def test_utf16_and_embedded_nul_are_not_assessed_as_empty(self):
        for data in ['[preset00]\nfWaveAlpha=1'.encode('utf-16'), HEADER + b'\0' + WARP]:
            with self.subTest(data=data[:12]):
                parsed = scan.parse_preset(data)
                self.assertIn('encoding', parsed['flags'])
                self.assertFalse(parsed['stages'])

    def test_zero_output_in_comment_is_not_a_formula_failure(self):
        parsed = scan.parse_preset(HEADER + b'warp_1=`shader_body {\nwarp_2=`// ret -= ret;\nwarp_3=`ret = GetPixel(uv); }\n')
        self.assertNotIn('zero_output_expression', parsed['flags'])

    def test_explicit_zero_output_is_a_review_hint(self):
        parsed = scan.parse_preset(HEADER + b'warp_1=`shader_body { ret -= ret; }\n')
        self.assertIn('zero_output_expression', parsed['flags'])

    def test_last_supported_custom_slot_is_a_static_drawing_source(self):
        for kind in ('wave', 'shape'):
            with self.subTest(kind=kind):
                parsed = scan.parse_preset(
                    f'[preset00]\nfWaveAlpha=0\n{kind}code_15_enabled=1\n'.encode())
                self.assertNotIn('no_static_drawing_source', parsed['flags'])

    def test_custom_slot_beyond_supported_range_is_not_a_drawing_source(self):
        for kind in ('wave', 'shape'):
            with self.subTest(kind=kind):
                parsed = scan.parse_preset(
                    f'[preset00]\nfWaveAlpha=0\n{kind}code_16_enabled=1\n'.encode())
                self.assertIn('no_static_drawing_source', parsed['flags'])

    def test_disabled_last_slot_dynamic_equations_remain_a_visual_review_hint(self):
        for kind in ('wave', 'shape'):
            with self.subTest(kind=kind):
                parsed = scan.parse_preset(
                    f'[preset00]\nfWaveAlpha=0\n{kind}code_15_enabled=0\n{kind}_15_per_frame1=a=1;\n'.encode())
                self.assertIn('no_static_drawing_source', parsed['flags'])
                self.assertTrue(any('Per-frame equations may change visibility' in note for note in parsed['notes']))


class RecoveryAssessmentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='milkdrop-recovery-tests-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.root = self.base / 'foobar-profile' / 'milkdrop2'
        self.presets = self.root / 'presets'
        self.presets.mkdir(parents=True)
        self.output = self.base / 'reports'
        self.output.mkdir()
        self.compiler = self.base / 'compiler.exe'
        self.compiler.write_bytes(b'compiler fixture; never executed')
        self.include = self.base / 'include.fx'
        self.include.write_bytes(b'include fixture')
        self.number = 0

    def fixture(self, name='example.milk', data=HEADER + WARP):
        path = self.presets / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def assess(self, name, paths, status='passed', old=None):
        self.number += 1
        def compiled(compiler, include, output, stage, details, timeout):
            return {'status': status, 'profile': details['profile'], 'declared_version': details['declared_version'], 'diagnostic': 'fixture diagnostic'}
        with patch.object(scan, 'compile_stage', side_effect=compiled) as mock_compile:
            row = scan.assess(name, paths, self.output / str(self.number), self.compiler, self.include, 5, old or {})
        return row, mock_compile.call_count

    def test_empty_missing_unreadable_and_ambiguous_are_distinct(self):
        empty = self.fixture('empty.milk', b'')
        unreadable = self.fixture('unreadable.milk')
        duplicate1 = self.fixture('one/shared.milk')
        duplicate2 = self.fixture('two/shared.milk', HEADER + b'warp_1=`shader_body { ret = 0; }\n')
        self.assertEqual(self.assess('empty.milk', [empty])[0]['status'], 'empty')
        self.assertEqual(self.assess('missing.milk', [])[0]['status'], 'missing')
        row, calls = self.assess('shared.milk', [duplicate1, duplicate2])
        self.assertEqual(row['status'], 'ambiguous')
        self.assertEqual(calls, 0)
        self.assertEqual(set(row['paths']), {str(duplicate1), str(duplicate2)})
        with patch.object(Path, 'read_bytes', side_effect=PermissionError('fixture access denied')):
            row, calls = self.assess('unreadable.milk', [unreadable])
        self.assertEqual(row['status'], 'unreadable')
        self.assertEqual(calls, 0)

    def test_index_preserves_ambiguous_case_insensitive_basenames(self):
        first = self.fixture('one/Example.milk')
        second = self.fixture('two/example.MILK')
        self.fixture('ignored.milk')
        found = scan.index_presets(self.presets, {'example.milk'})
        self.assertEqual(set(found['example.milk']), {first, second})
        self.assertNotIn('ignored.milk', found)

    def test_names_accept_windows_and_forward_slash_paths(self):
        listing = self.root / 'preset-blacklist.txt'
        listing.write_text('# comment\n C:\\pack\\Example.milk \nother/EXAMPLE.MILK\n\n', encoding='utf-8-sig')
        self.assertEqual(set(scan.read_names(listing)), {'example.milk'})

    def test_successful_shader_compile_is_only_a_live_candidate(self):
        path = self.fixture()
        row, calls = self.assess(path.name, [path])
        self.assertEqual(row['status'], 'live_test_candidate')
        self.assertEqual(calls, 1)
        self.assertNotIn('recovered', scan.LABELS[row['status']].lower())

    def test_fresh_failure_timeout_and_tool_failure_are_not_candidates(self):
        path = self.fixture()
        for compilation, expected in [('failed', 'shader_repair'), ('timeout', 'incomplete'), ('tool_error', 'incomplete')]:
            with self.subTest(compilation=compilation):
                row, _ = self.assess(path.name, [path], status=compilation)
                self.assertEqual(row['status'], expected)

    def test_metadata_disabled_shader_is_not_a_live_candidate(self):
        path = self.fixture(data=b'[preset00]\n' + WARP)
        row, calls = self.assess(path.name, [path])
        self.assertEqual(row['status'], 'metadata_review')
        self.assertEqual(calls, 1)

    def test_duplicate_rating_is_informational_not_a_recovery_block(self):
        path = self.fixture(data=HEADER + b'fRating=1\nfRating=5\n' + WARP)
        row, calls = self.assess(path.name, [path])
        self.assertEqual(row['status'], 'live_test_candidate')
        self.assertEqual(calls, 1)
        self.assertNotIn('duplicate_keys', row['flags'])
        self.assertTrue(any('fRating' in note for note in row['notes']))

    def test_importer_code_truncation_blocks_live_candidate_even_if_prefix_compiles(self):
        path = self.fixture(data=HEADER + b'warp_1=' + b'a' * 40000 + b'\n')
        row, calls = self.assess(path.name, [path])
        self.assertEqual(row['status'], 'metadata_review')
        self.assertEqual(calls, 1)
        self.assertIn('code_limit', row['flags'])

    def test_bom_and_noncanonical_keys_do_not_silently_enable_shaders(self):
        cases = [
            b'\xef\xbb\xbf' + HEADER + WARP,
            b' MILKDROP_PRESET_VERSION=201\nPSVERSION_WARP=2\n' + WARP,
            b'milkdrop_preset_version=201\nPSVERSION_WARP=2\n' + WARP,
        ]
        for index, data in enumerate(cases):
            with self.subTest(index=index):
                path = self.fixture(f'noncanonical-{index}.milk', data)
                row, _ = self.assess(path.name, [path])
                self.assertEqual(row['status'], 'metadata_review')

    def test_absent_section_is_not_classed_as_broken(self):
        path = self.fixture(data=b'wavecode_0_enabled=1\nwavecode_0_a=1\n')
        row, calls = self.assess(path.name, [path])
        self.assertEqual(row['status'], 'legacy_candidate')
        self.assertEqual(calls, 0)

    def test_dynamic_visibility_is_not_claimed_to_be_definitely_blank(self):
        path = self.fixture(data=b'[preset00]\nfWaveAlpha=0\nper_frame_1=wave_a=1;\n')
        row, _ = self.assess(path.name, [path])
        self.assertIn(row['status'], ('legacy_candidate', 'visual_review'))
        notes = ' '.join(row['notes']).lower()
        self.assertNotIn('no dynamic equations', notes)
        self.assertNotIn('confirmed blank', notes.replace('not a confirmed blank', ''))

    def test_historical_priority_requires_matching_current_bytes(self):
        path = self.fixture()
        current = hashlib.sha256(path.read_bytes()).hexdigest()
        row, _ = self.assess(path.name, [path], old={path.name: current})
        self.assertEqual(row['priority'], 'High')
        row, _ = self.assess(path.name, [path], old={path.name: '0' * 64})
        self.assertEqual(row['priority'], 'Normal')

    def test_mid_scan_file_change_invalidates_result(self):
        path = self.fixture()
        with patch.object(Path, 'read_bytes', side_effect=[HEADER + WARP, HEADER + WARP + b'; changed']):
            row, _ = self.assess(path.name, [path])
        self.assertEqual(row['status'], 'incomplete')
        self.assertTrue(any('changed' in note for note in row['notes']))

    def test_oversized_file_is_not_claimed_empty_or_unrecoverable(self):
        path = self.fixture(data=b'a' * 16)
        with patch.object(scan, 'MAX_PRESET_BYTES', 8):
            row, calls = self.assess(path.name, [path])
        self.assertEqual(row['status'], 'incomplete')
        self.assertEqual(calls, 0)

    def test_signed_32bit_metadata_overflow_requires_review_without_compiling(self):
        cases = [
            b'MILKDROP_PRESET_VERSION=2147483648\n',
            b'MILKDROP_PRESET_VERSION=201\nPSVERSION_WARP=2147483648\n',
            b'MILKDROP_PRESET_VERSION=201\nPSVERSION_WARP=-2147483649\n',
        ]
        for index, header in enumerate(cases):
            with self.subTest(header=header):
                path = self.fixture(f'overflow-{index}.milk', header + WARP)
                row, calls = self.assess(path.name, [path])
                self.assertEqual(row['status'], 'metadata_review')
                self.assertEqual(calls, 0)
                self.assertTrue(any('integer range' in note for note in row['notes']))

    def test_five_thousand_digit_metadata_does_not_abort_other_presets(self):
        self.fixture('huge-integer.milk', b'MILKDROP_PRESET_VERSION=' + b'9' * 5000 + b'\n' + WARP)
        self.fixture('valid.milk', b'[preset00]\nfWaveAlpha=1\n')
        (self.root / 'preset-blacklist.txt').write_text('huge-integer.milk\nvalid.milk\n')
        target = self.base / 'huge-integer-report'
        with patch.object(sys, 'argv', self.arguments(target)), patch.object(scan, 'historical_candidates', return_value={}), patch.object(scan, 'compile_stage') as compiler, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(scan.main(), 0)
        compiler.assert_not_called()
        report = json.loads((target / 'results.json').read_text())
        self.assertTrue(report['completed'])
        self.assertEqual(report['assessed_names'], 2)
        self.assertEqual(report['counts'], {'legacy_candidate': 1, 'metadata_review': 1})
        self.assertIn('valid.milk', (target / 'live-test-candidates.txt').read_text())
        self.assertNotIn('huge-integer.milk', (target / 'live-test-candidates.txt').read_text())

    def test_compiler_launch_failure_is_tool_error(self):
        work = self.output / 'compiler-launch'
        work.mkdir()
        details = {'raw': b'shader_body {}', 'profile': 'ps_4_0', 'declared_version': 2}
        with patch.object(scan.subprocess, 'run', side_effect=FileNotFoundError('compiler unavailable')):
            result = scan.compile_stage(self.compiler, self.include, work, 'warp', details, 5)
        self.assertEqual(result['status'], 'tool_error')

    def test_compiler_timeout_remains_unknown(self):
        work = self.output / 'compiler-timeout'
        work.mkdir()
        details = {'raw': b'shader_body {}', 'profile': 'ps_4_0', 'declared_version': 2}
        with patch.object(scan.subprocess, 'run', side_effect=subprocess.TimeoutExpired('compiler', 5)):
            result = scan.compile_stage(self.compiler, self.include, work, 'warp', details, 5)
        self.assertEqual(result['status'], 'timeout')

    def test_compiler_protocol_distinguishes_shader_rejection_from_tool_failure(self):
        cases = [(0, True, 'passed'), (0, False, 'tool_error'), (1, False, 'failed'), (3, False, 'failed'), (2, False, 'tool_error'), (4, False, 'tool_error')]
        for number, (returncode, bytecode, expected) in enumerate(cases):
            with self.subTest(returncode=returncode, bytecode=bytecode):
                work = self.output / f'compiler-protocol-{number}'
                work.mkdir()
                details = {'raw': b'shader_body {}', 'profile': 'ps_4_0', 'declared_version': 2}
                def run(command, **kwargs):
                    if bytecode:
                        Path(command[-1]).write_bytes(b'compiled-bytecode-fixture')
                    return subprocess.CompletedProcess(command, returncode, b'fixture stdout\0', b'fixture stderr')
                with patch.object(scan.subprocess, 'run', side_effect=run):
                    result = scan.compile_stage(self.compiler, self.include, work, 'warp', details, 5)
                self.assertEqual(result['status'], expected)
                self.assertNotIn('\0', result['diagnostic'])

    def test_report_escapes_names_notes_and_compiler_diagnostics(self):
        row = {'name': '<script>alert(1)</script>.milk', 'paths': ['<bad-path>'], 'status': 'shader_repair', 'priority': 'Normal', 'bytes': 1, 'sha256': 'abc', 'notes': ['<b>untrusted</b>'], 'flags': [], 'shaders': {'warp': {'profile': 'ps_4_0', 'status': 'failed', 'diagnostic': '<img src=x onerror=alert(1)>'}}}
        context = {'limits': 'test limits', 'created': 'test time', 'scope': '<scope>'}
        scan.write_reports(self.output, [row], context)
        report = (self.output / 'report.html').read_text(encoding='utf-8')
        for unsafe in [row['name'], row['notes'][0], row['shaders']['warp']['diagnostic'], '<scope>']:
            self.assertNotIn(unsafe, report)
        self.assertIn('&lt;script&gt;', report)
        self.assertIn('&lt;img', report)
        self.assertNotIn(row['name'], (self.output / 'live-test-candidates.txt').read_text())

    def test_csv_formula_prefixes_are_escaped_without_changing_json_evidence(self):
        rows = []
        prefixes = ['=', '+', '-', '@', '  =', '\t+', '\r\n@']
        for prefix in prefixes:
            rows.append({'name': prefix + 'SUM(1,2).milk', 'paths': [prefix + 'untrusted-path'], 'status': 'legacy_candidate', 'priority': 'Normal', 'bytes': 12, 'sha256': 'abc', 'notes': [prefix + 'untrusted-note'], 'flags': [], 'shaders': {}})
        rows.append({'name': 'ordinary.milk', 'paths': ['C:\\presets\\ordinary.milk'], 'status': 'legacy_candidate', 'priority': 'Normal', 'bytes': 12, 'sha256': 'abc', 'notes': ["'already quoted"], 'flags': [], 'shaders': {}})
        scan.write_reports(self.output, rows, {'limits': 'test', 'created': 'test', 'scope': 'test'})
        with (self.output / 'results.csv').open(encoding='utf-8-sig', newline='') as stream:
            csv_rows = list(csv.DictReader(stream))
        for row in csv_rows:
            if row['Preset'] == 'ordinary.milk':
                self.assertEqual(row['Paths'], 'C:\\presets\\ordinary.milk')
                self.assertEqual(row['Notes'], "'already quoted")
                continue
            for field in ('Preset', 'Notes', 'Paths'):
                self.assertTrue(row[field].startswith("'"), (field, row[field]))
                self.assertTrue(row[field][1:].lstrip().startswith(('=', '+', '-', '@')))
        json_rows = json.loads((self.output / 'results.json').read_text())['presets']
        self.assertEqual({row['name'] for row in json_rows}, {row['name'] for row in rows})
        self.assertEqual({tuple(row['notes']) for row in json_rows}, {tuple(row['notes']) for row in rows})

    def arguments(self, output):
        return ['milkdrop_recovery_scan.py', '--milkdrop-root', str(self.root), '--output-directory', str(output), '--compiler', str(self.compiler), '--include-file', str(self.include), '--workers', '1']

    def test_cli_rejects_output_inside_live_profile(self):
        target = self.root / 'diagnostic-output'
        with patch.object(sys, 'argv', self.arguments(target)):
            with self.assertRaisesRegex(ValueError, 'outside'):
                scan.main()
        self.assertFalse(target.exists())

    def test_cli_rejects_existing_report_directory(self):
        with patch.object(sys, 'argv', self.arguments(self.output)):
            with self.assertRaisesRegex(ValueError, 'already exists'):
                scan.main()

    def test_read_only_cli_smoke_preserves_all_fixture_profile_bytes(self):
        self.fixture('legacy.milk', b'[preset00]\nfWaveAlpha=1\n')
        self.fixture('empty.milk', b'')
        self.fixture('one/shared.milk', b'[preset00]\nfWaveAlpha=1\n')
        self.fixture('two/shared.milk', b'[preset00]\nfWaveAlpha=0\n')
        (self.root / 'preset-blacklist.txt').write_text('legacy.milk\nempty.milk\nmissing.milk\nshared.milk\n')
        (self.root / 'runtime-safety-cache.txt').write_text('visual-inactive\t1\tlegacy.milk\n')
        before = {path.relative_to(self.root): path.read_bytes() for path in self.root.rglob('*') if path.is_file()}
        target = self.base / 'complete-report'
        with patch.object(sys, 'argv', self.arguments(target)), patch.object(scan, 'historical_candidates', return_value={}), patch.object(scan, 'compile_stage') as compiler, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(scan.main(), 0)
        compiler.assert_not_called()
        after = {path.relative_to(self.root): path.read_bytes() for path in self.root.rglob('*') if path.is_file()}
        self.assertEqual(before, after)
        report = json.loads((target / 'results.json').read_text())
        self.assertEqual(report['assessed_names'], 4)
        self.assertEqual(report['counts'], {'ambiguous': 1, 'empty': 1, 'legacy_candidate': 1, 'missing': 1})
        self.assertTrue(report['completed'])
        self.assertIn('legacy.milk', (target / 'live-test-candidates.txt').read_text())
        self.assertNotIn('shared.milk', (target / 'live-test-candidates.txt').read_text())


if __name__ == '__main__':
    unittest.main()
