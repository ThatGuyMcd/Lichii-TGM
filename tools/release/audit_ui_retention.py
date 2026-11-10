#!/usr/bin/env python3
"""Verify the Forest Lantern layout retains the v1.0.1 forms and apply paths."""

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default='Lichii-tgm-v1.0.1')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]

    def git(*arguments):
        return subprocess.check_output(['git', '-C', str(root), *arguments])

    baseline = git('rev-parse', args.baseline + '^{commit}').decode().strip()
    paths = git('ls-tree', '-r', '--name-only', baseline, 'src/yuzu').decode().splitlines()
    forms = []
    widget_count = 0
    for name in paths:
        if not name.endswith('.ui'):
            continue
        original = git('show', f'{baseline}:{name}')
        current = (root / name).read_bytes()
        # Line-ending conversion by Git is harmless; every UI element/property must match.
        old = ET.tostring(ET.fromstring(original))
        new = ET.tostring(ET.fromstring(current))
        assert old == new, f'Original UI form changed: {name}'
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
        assert original.replace(b'\r\n', b'\n') == (root / path).read_bytes().replace(b'\r\n', b'\n'), path

    result = {
        'baseline_commit': baseline,
        'source_commit': git('rev-parse', 'HEAD').decode().strip(),
        'unchanged_ui_forms': len(forms),
        'retained_form_widgets': widget_count,
        'unchanged_apply_paths': list(apply_paths),
        'emulation_setting_definitions_unchanged': True,
        'forms_digest': hashlib.sha256('\n'.join(forms).encode()).hexdigest(),
        'note': 'Navigation/theme runtime retention is covered separately by standalone Qt tests.',
    }
    content = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(content, encoding='utf-8')
    print(content, end='')


if __name__ == '__main__':
    main()
