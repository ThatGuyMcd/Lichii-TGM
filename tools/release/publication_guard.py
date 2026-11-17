#!/usr/bin/env python3
"""Fail closed on publication hazards; report paths/rules, never secret values."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import zipfile

GAME_SUFFIXES = {'.nsp', '.nsz', '.xci', '.xcz', '.nca', '.nso', '.nro', '.nup',
                 '.kip', '.tik', '.rdc', '.dmp', '.z64', '.v64', '.n64', '.nes',
                 '.sfc', '.smc', '.gba', '.nds', '.3ds', '.cia', '.wad', '.wud'}
OPAQUE_SUFFIXES = {'.bin', '.spv', '.env', '.record', '.cache', '.profdata', '.nx2recipe'}
MEDIA_SUFFIXES = {'.png', '.jpg', '.jpeg', '.webp', '.gif', '.ico', '.icns', '.svg',
                  '.ttf', '.otf', '.wav', '.mp3', '.ogg', '.mp4'}
SECRET_FILES = {'prod.keys', 'title.keys', 'console.keys', 'keys.dat', '.env'}
PRIVATE_ROOTS = {'user', 'nand', 'sdmc', 'screenshots', 'captures', 'game_dumps', 'crashdumps'}
OWNER = re.compile(r'(?i)\bcopyright\s*(?:\(c\)|©)?\s*(?:(?:19|20)\d{2}(?:[,\s-]*(?:19|20)\d{2})?\s*)?nintendo\b|\bnintendo\s+confidential\b')
SECRETS = re.compile(r'\bgh[pousr]_[A-Za-z0-9]{36,}\b|\bgithub_pat_[A-Za-z0-9_]{70,}\b|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')
TEXT_LIMIT = 32 * 1024 * 1024

def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])

def issue(path, rule):
    return {'path': path, 'rule': rule}

def inspect_bytes(name, data, package=False):
    path = PurePosixPath(name.replace('\\', '/'))
    findings = []
    if path.is_absolute() or '..' in path.parts or re.match(r'^[A-Za-z]:', str(path)):
        findings.append(issue(name, 'unsafe_archive_path'))
    if path.suffix.lower() in GAME_SUFFIXES or path.name.lower() in SECRET_FILES:
        findings.append(issue(name, 'game_system_capture_or_private_file'))
    private_parts = path.parts if package else path.parts[:1]
    if any(part.lower() in PRIVATE_ROOTS for part in private_parts):
        findings.append(issue(name, 'private_user_data_directory'))
    if data[:4] in (b'NSO0', b'KIP1', b'NCA2', b'NCA3') or data[16:20] == b'NRO0':
        findings.append(issue(name, 'console_executable_magic'))
    if b'\0' not in data[:8192] and len(data) <= TEXT_LIMIT:
        text = data.decode('utf-8', errors='replace')
        if OWNER.search(text):
            findings.append(issue(name, 'nintendo_ownership_or_confidential_marker'))
        if SECRETS.search(text):
            findings.append(issue(name, 'credential_or_private_key'))
        if path.suffix.lower() == '.json':
            try:
                document = json.loads(text)
            except (ValueError, RecursionError):
                document = None
            if isinstance(document, dict) and document.get('source_cache_sha256'):
                environment = document.get('environment')
                if isinstance(environment, dict) and environment.get('code'):
                    findings.append(issue(name, 'captured_game_shader_instructions'))
    return findings

def reviewed(path, data, entries):
    record = entries.get(path, {})
    return (record.get('sha256') == hashlib.sha256(data).hexdigest()
            and record.get('origin') and record.get('license_or_data_basis')
            and record.get('classification') in ('independent_artwork', 'licensed_dependency',
                                                 'synthetic_fixture', 'emulator_metadata'))

def scan_source(repo, baseline=None, require_clean=False):
    findings = []
    entries_path = repo / 'tools/release/publication-provenance.json'
    entries = json.loads(entries_path.read_text()).get('files', {}) if entries_path.exists() else {}
    if require_clean and git(repo, 'status', '--porcelain=v1', '--untracked-files=all').strip():
        findings.append(issue('.', 'dirty_worktree'))
    if not (repo / 'LICENSE.txt').is_file():
        findings.append(issue('LICENSE.txt', 'missing_project_license'))
    names = [os.fsdecode(x) for x in git(repo, 'ls-files', '-z').split(b'\0') if x]
    changed = set()
    if baseline:
        git(repo, 'rev-parse', '--verify', baseline + '^{commit}')
        changed = {os.fsdecode(x) for x in git(repo, 'diff', '--name-only', '--diff-filter=ACMRT',
                                             '-z', baseline, 'HEAD').split(b'\0') if x}
    for name in names:
        path = repo / name
        if path.is_dir():
            findings.append(issue(name, 'dependency_checkout_requires_provenance_review'))
            continue
        if not path.is_file() or path.is_symlink():
            if path.is_symlink():
                target = path.resolve()
                if not target.is_relative_to(repo.resolve()):
                    findings.append(issue(name, 'source_symlink_outside_repository'))
            continue
        data = path.read_bytes()
        suffix = path.suffix.lower()
        observed = inspect_bytes(name, data)
        record = entries.get(name, {})
        if (name.startswith('src/tests/') and suffix in {'.nsp', '.xci', '.nup'}
                and record.get('classification') == 'synthetic_fixture'
                and reviewed(name, data, entries)):
            observed = [r for r in observed if r['rule'] != 'game_system_capture_or_private_file']
        findings.extend(observed)
        changed_payload = name in changed and (suffix in MEDIA_SUFFIXES or b'\0' in data[:8192])
        if suffix in OPAQUE_SUFFIXES or changed_payload:
            if not reviewed(name, data, entries):
                findings.append(issue(name, 'payload_requires_exact_provenance_review'))
    return {'mode': 'source', 'source_commit': git(repo, 'rev-parse', 'HEAD').decode().strip(),
            'baseline': baseline, 'tracked_files_scanned': len(names), 'findings': findings,
            'passed': not findings, 'scope': 'Content/provenance tripwires, not a legal clearance certificate'}

def scan_tree(tree):
    findings = []
    count = 0
    for path in tree.rglob('*'):
        name = path.relative_to(tree).as_posix()
        if path.is_symlink():
            if not path.resolve().is_relative_to(tree.resolve()):
                findings.append(issue(name, 'package_symlink_outside_tree'))
            continue
        if path.is_file():
            count += 1
            findings.extend(inspect_bytes(name, path.read_bytes(), package=True))
    return {'mode': 'package_tree', 'files_scanned': count, 'findings': findings,
            'passed': not findings, 'scope': 'Package tripwires supplement source provenance and license checks'}

def scan_zip(archive):
    findings = []
    with zipfile.ZipFile(archive) as package:
        if package.testzip() is not None:
            findings.append(issue(archive.name, 'archive_crc_failure'))
        for member in package.infolist():
            if member.is_dir():
                continue
            findings.extend(inspect_bytes(member.filename, package.read(member), package=True))
    return {'mode': 'zip', 'findings': findings, 'passed': not findings}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    source = sub.add_parser('source')
    source.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    source.add_argument('--baseline')
    source.add_argument('--require-clean', action='store_true')
    source.add_argument('--report', type=Path, required=True)
    tree = sub.add_parser('tree')
    tree.add_argument('path', type=Path)
    tree.add_argument('--report', type=Path, required=True)
    archive = sub.add_parser('zip')
    archive.add_argument('path', type=Path)
    archive.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'source':
        report = scan_source(args.repo.resolve(), args.baseline, args.require_clean)
    elif args.mode == 'tree':
        report = scan_tree(args.path.resolve())
    else:
        report = scan_zip(args.path)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print('Publication content checks passed.' if report['passed'] else
          'Publication requirements not met; inspect the private report.')
    return 0 if report['passed'] else 1

if __name__ == '__main__':
    sys.exit(main())
