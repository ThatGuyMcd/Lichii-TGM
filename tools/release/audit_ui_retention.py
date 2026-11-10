#!/usr/bin/env python3
"""Verify the Forest Lantern layout retains the v1.0.1 forms and apply paths."""

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default='Lichii-tgm-v1.0.1')
    parser.add_argument('--upstream', help='Reviewed upstream commit whose additions are permitted')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]

    def git(*arguments):
        return subprocess.check_output(['git', '-C', str(root), *arguments])

    baseline = git('rev-parse', args.baseline + '^{commit}').decode().strip()
    upstream = git('rev-parse', args.upstream + '^{commit}').decode().strip() if args.upstream else None
    upstream_base = git('merge-base', baseline, upstream).decode().strip() if upstream else None
    upstream_paths = set(git('diff', '--name-only', upstream_base, upstream).decode().splitlines()) \
        if upstream else set()

    def expected_content(path, original):
        if path not in upstream_paths:
            return original
        # Permit precisely the reviewed upstream delta, preserving the fork's baseline content.
        with tempfile.TemporaryDirectory(prefix='ui-retention-') as directory:
            files = [pathlib.Path(directory) / name for name in ['fork', 'base', 'upstream']]
            for file, content in zip(files, [original, git('show', f'{upstream_base}:{path}'),
                                            git('show', f'{upstream}:{path}')]):
                file.write_bytes(content.replace(b'\r\n', b'\n'))
            merged = subprocess.run(['git', 'merge-file', '-p', *map(str, files)], capture_output=True)
            assert merged.returncode == 0, f'Upstream retention baseline conflicts: {path}'
            return merged.stdout

    paths = git('ls-tree', '-r', '--name-only', baseline, 'src/yuzu').decode().splitlines()
    forms = []
    updated_forms = []
    widget_count = 0
    for name in paths:
        if not name.endswith('.ui'):
            continue
        original = git('show', f'{baseline}:{name}')
        current = (root / name).read_bytes()
        # Line-ending conversion by Git is harmless; every UI element/property must match.
        old = ET.tostring(ET.fromstring(expected_content(name, original)))
        new = ET.tostring(ET.fromstring(current))
        assert old == new, f'UI form differs from baseline plus reviewed upstream: {name}'
        if ET.tostring(ET.fromstring(original)) != new:
            updated_forms.append(name)
        widget_count += len(list(ET.fromstring(current).iter('widget')))
        forms.append(name)

    apply_paths = {
        'src/yuzu/configuration/configure_dialog.cpp': 'void ConfigureDialog::ApplyConfiguration()',
        'src/yuzu/configuration/configure_per_game.cpp': 'void ConfigurePerGame::ApplyConfiguration()',
        'src/yuzu/configuration/configure_input.cpp': 'void ConfigureInput::ApplyConfiguration()',
    }

    def function(text, signature):
        start = text.index(signature)
        brace = text.index('{', start)
        depth = 1
        end = brace + 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        return re.sub(r'\s+', '', text[start:end])

    for path, signature in apply_paths.items():
        old = git('show', f'{baseline}:{path}').decode()
        new = (root / path).read_text(encoding='utf-8')
        assert function(old, signature) == function(new, signature), f'Apply path changed: {path}'

    for path in ['src/common/settings.h', 'src/common/settings_enums.h']:
        original = git('show', f'{baseline}:{path}')
        expected = expected_content(path, original)
        if path.endswith('settings_enums.h'):
            old_aspects = b'ENUM(AspectRatio, R16_9, R4_3, R21_9, R16_10, Stretch);'
            new_aspects = (
                b'// Keep existing values stable: saved configurations store their numeric IDs.\n'
                b'ENUM(AspectRatio, R16_9, R4_3, R21_9, R16_10, Stretch, R3_2, R2_1, R32_9, R32_10);'
            )
            assert expected.count(old_aspects) == 1
            expected = expected.replace(old_aspects, new_aspects)
        assert expected.replace(b'\r\n', b'\n') == (root / path).read_bytes().replace(b'\r\n', b'\n'), path

    result = {
        'baseline_commit': baseline,
        'source_commit': git('rev-parse', 'HEAD').decode().strip(),
        'reviewed_upstream_commit': upstream,
        'retained_ui_forms': len(forms),
        'unchanged_ui_forms': len(forms) - len(updated_forms),
        'upstream_updated_ui_forms': updated_forms,
        'retained_form_widgets': widget_count,
        'unchanged_apply_paths': list(apply_paths),
        'setting_definitions_match_baseline_plus_upstream': True,
        'reviewed_setting_extensions': {'AspectRatio': ['R3_2', 'R2_1', 'R32_9', 'R32_10']},
        'forms_digest': hashlib.sha256('\n'.join(forms).encode()).hexdigest(),
        'note': 'Navigation/theme runtime retention is covered separately by standalone Qt tests.',
    }
    content = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(content, encoding='utf-8')
    print(content, end='')


if __name__ == '__main__':
    main()
