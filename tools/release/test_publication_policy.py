import importlib.util
import json
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

HERE = Path(__file__).resolve().parent
def module(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result
guard = module('publication_guard')
planner = module('plan_nightly_release')

class PublicationPolicyTests(unittest.TestCase):
    def rules(self, name, data=b'', package=False):
        return {row['rule'] for row in guard.inspect_bytes(name, data, package)}

    def test_normal_emulator_source_and_brand_names_are_allowed(self):
        self.assertFalse(self.rules('src/core/compat.cpp', b'// Independent Nintendo Switch compatibility implementation'))
        self.assertFalse(self.rules('src/core/crypto/keys.h', b'// Key parser, not private key material'))

    def test_game_and_private_paths_are_blocked_case_insensitively(self):
        for name in ('game.NSP', 'content/guest.NSO', 'prod.keys', 'frame.rdc', 'crash.DMP'):
            self.assertIn('game_system_capture_or_private_file', self.rules(name))
        self.assertIn('private_user_data_directory', self.rules('app/user/save.dat', package=True))
        self.assertIn('unsafe_archive_path', self.rules('../outside.txt'))
        self.assertIn('unsafe_archive_path', self.rules('C:/outside.txt'))

    def test_owner_markers_and_console_magic_are_blocked(self):
        comment = ('// Copy' + 'right (c) 2026 ' + 'Nin' + 'tendo Co., Ltd.').encode()
        self.assertIn('nintendo_ownership_or_confidential_marker', self.rules('file.cpp', comment))
        self.assertIn('console_executable_magic', self.rules('renamed.dat', b'NSO0' + bytes(40)))

    def test_captured_instructions_block_but_metadata_does_not(self):
        data = {'source_cache_sha256': '0' * 64, 'environment': {'code': ['synthetic-test-token']}}
        self.assertIn('captured_game_shader_instructions', self.rules('capture.json', json.dumps(data).encode()))
        data['environment'].pop('code')
        self.assertFalse(self.rules('recipe.json', json.dumps(data).encode()))

    def test_findings_never_echo_private_values(self):
        token = 'gh' + 'p_' + 'a' * 40
        result = guard.inspect_bytes('config.txt', token.encode())
        self.assertIn('credential_or_private_key', {r['rule'] for r in result})
        self.assertNotIn(token, json.dumps(result))

    def test_provenance_binds_exact_bytes(self):
        import hashlib
        entries = {'asset.bin': {'sha256': hashlib.sha256(b'example').hexdigest(),
                   'classification': 'synthetic_fixture', 'origin': 'hand-authored',
                   'license_or_data_basis': 'GPL-3.0-or-later'}}
        self.assertTrue(guard.reviewed('asset.bin', b'example', entries))
        self.assertFalse(guard.reviewed('asset.bin', b'changed', entries))
        self.assertFalse(guard.reviewed('other.bin', b'example', entries))

    def test_nested_package_data_and_renamed_console_programs_block(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / 'package.zip'
            with zipfile.ZipFile(archive, 'w') as package:
                package.writestr('app/user/save.dat', b'synthetic')
                package.writestr('app/data/renamed.dat', b'NSO0' + bytes(40))
                package.writestr('../outside.txt', b'synthetic')
            result = guard.scan_zip(archive)
            self.assertFalse(result['passed'])
            self.assertEqual(len(result['findings']), 3)

    def test_dirty_and_tracked_game_files_are_real_git_findings(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            subprocess.run(['git', 'init', '-q', str(repo)], check=True)
            (repo / 'LICENSE.txt').write_text('Synthetic test license')
            (repo / 'game.nsp').write_bytes(b'Synthetic, not game content')
            subprocess.run(['git', '-C', str(repo), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Policy fixture',
                            '-c', 'user.email=fixture@example.invalid', 'commit', '-qm',
                            'Synthetic fixture'], check=True)
            (repo / 'LICENSE.txt').write_text('Changed synthetic test license')
            result = guard.scan_source(repo, require_clean=True)
            self.assertFalse(result['passed'])
            self.assertIn('dirty_worktree', {r['rule'] for r in result['findings']})
            self.assertIn('game_system_capture_or_private_file', {r['rule'] for r in result['findings']})

    def test_numeric_next_version_ignores_upstream_prereleases_and_drafts(self):
        releases = [{'tag_name': tag, **extra} for tag, extra in [
            ('Lichii-tgm-v1.0.9', {}), ('Lichii-tgm-v1.0.3', {}),
            ('build-316', {}), ('Lichii-tgm-v9.0.0', {'prerelease': True}),
            ('Lichii-tgm-v1.0.10', {'draft': True, 'id': 7})]]
        result = planner.plan(releases, {}, True)
        self.assertEqual(result['version'], '1.0.10')
        self.assertEqual(result['action'], 'resume_draft')
        self.assertEqual(result['draft_id'], 7)

    def test_synthetic_container_exception_is_source_only_and_hash_bound(self):
        import hashlib
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            subprocess.run(['git', 'init', '-q', str(repo)], check=True)
            name = 'src/tests/example.nsp'
            fixture = repo / name
            fixture.parent.mkdir(parents=True)
            fixture.write_bytes(b'Independently constructed synthetic fixture')
            (repo / 'LICENSE.txt').write_text('Synthetic fixture license')
            policy = repo / 'tools/release/publication-provenance.json'
            policy.parent.mkdir(parents=True)
            policy.write_text(json.dumps({'files': {name: {
                'sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
                'classification': 'synthetic_fixture', 'origin': 'hand-authored test',
                'license_or_data_basis': 'independent test data'}}}))
            subprocess.run(['git', '-C', str(repo), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Policy fixture',
                            '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'Fixture'], check=True)
            self.assertTrue(guard.scan_source(repo, require_clean=True)['passed'])
            self.assertIn('game_system_capture_or_private_file', self.rules(name, fixture.read_bytes(), package=True))
            fixture.write_bytes(b'Different payload')
            self.assertFalse(guard.scan_source(repo)['passed'])

    def test_no_changes_and_retries_do_not_consume_versions(self):
        now = datetime(2026, 11, 17, tzinfo=timezone.utc)
        self.assertEqual(planner.plan([], {}, False, now)['action'], 'no_changes')
        self.assertEqual(planner.plan([], {'runs': [{'london_date': '2026-11-17',
            'status': 'published'}]}, True, now)['action'], 'already_published')
        with self.assertRaises(ValueError):
            planner.plan([], {}, True, now)

    def test_london_date_in_summer_is_not_utc_date(self):
        now = datetime(2026, 6, 1, 23, 30, tzinfo=timezone.utc)
        self.assertEqual(planner.plan([], {}, False, now)['london_date'], '2026-06-02')

    def test_published_api_record_prevents_duplicate_after_journal_loss(self):
        now = datetime(2026, 11, 17, 2, tzinfo=timezone.utc)
        releases = [{'tag_name': 'Lichii-tgm-v1.0.4', 'published_at': '2026-11-17T01:40:00Z'}]
        self.assertEqual(planner.plan(releases, {}, True, now)['action'], 'already_published')

if __name__ == '__main__':
    unittest.main()
