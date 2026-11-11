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
    parser.add_argument('--ui-upstream', help='UI integration boundary when later upstream layouts are retained locally')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]

    def git(*arguments):
        return subprocess.check_output(['git', '-C', str(root), *arguments])

    baseline = git('rev-parse', args.baseline + '^{commit}').decode().strip()
    upstream = git('rev-parse', args.upstream + '^{commit}').decode().strip() if args.upstream else None
    ui_upstream = git('rev-parse', args.ui_upstream + '^{commit}').decode().strip() if args.ui_upstream else upstream
    if args.ui_upstream:
        assert upstream and git('merge-base', ui_upstream, upstream).decode().strip() == ui_upstream
    upstream_base = git('merge-base', baseline, ui_upstream).decode().strip() if ui_upstream else None
    upstream_paths = set(git('diff', '--name-only', upstream_base, ui_upstream).decode().splitlines()) \
        if ui_upstream else set()

    def expected_content(path, original):
        if path == 'src/yuzu/render/performance_overlay.ui':
            return original  # The fork retains its existing handheld overlay.
        if path not in upstream_paths:
            return original
        # Permit precisely the reviewed upstream delta, preserving the fork's baseline content.
        with tempfile.TemporaryDirectory(prefix='ui-retention-') as directory:
            files = [pathlib.Path(directory) / name for name in ['fork', 'base', 'upstream']]
            for file, content in zip(files, [original, git('show', f'{upstream_base}:{path}'),
                                            git('show', f'{ui_upstream}:{path}')]):
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

    def declarations(content):
        text = content.decode('utf-8')
        text = re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                      lambda match: match[0] if match[0].startswith('"') else '', text, flags=re.S)
        pattern = r'((?:Switchable)?Setting\s*<[^;{}]+?>\s+(\w+)\s*\{.*?\};)'
        found = re.findall(pattern, text, re.S)
        result = {name: re.sub(r'\s+', '', declaration) for declaration, name in found}
        assert len(result) == len(found), 'Duplicate setting declarations'
        return result

    path = 'src/common/settings.h'
    original_settings = declarations(git('show', f'{baseline}:{path}'))
    current_settings = declarations((root / path).read_bytes())
    incoming_settings = declarations(git('show', f'{upstream}:{path}')) if upstream else {}
    updated_settings = []
    assert len(original_settings) > 200, 'Setting parser did not read the complete baseline'
    for name, declaration in original_settings.items():
        if name == 'hdr_peak_luminance':
            declaration = declaration.replace('linkage,300,80,10000,', 'linkage,300,0,10000,')
        if current_settings.get(name) != declaration:
            incoming = incoming_settings.get(name)
            assert incoming and incoming != original_settings[name] and \
                current_settings.get(name) == incoming, f'Original setting changed or lost: {name}'
            updated_settings.append(name)
    if upstream:
        for name, declaration in incoming_settings.items():
            if name in original_settings:
                continue
            if name == 'nvidia_dlss_preset':
                declaration = declaration.replace('NvidiaDlssPreset::M,', 'NvidiaDlssPreset::NvidiaDefault,')
            assert current_settings.get(name) == declaration, f'Upstream setting changed or lost: {name}'

    def enumerations(content):
        text = re.sub(r'//[^\n]*', '', content.decode('utf-8'))
        return {name: [value.strip() for value in values.split(',')]
                for name, values in re.findall(r'^ENUM\((\w+),\s*([^()]+)\)\s*;?', text, re.M)}

    enum_path = 'src/common/settings_enums.h'
    current_enums = enumerations((root / enum_path).read_bytes())
    for reference in [baseline] + ([upstream] if upstream else []):
        for name, values in enumerations(git('show', f'{reference}:{enum_path}')).items():
            assert current_enums.get(name, [])[:len(values)] == values, f'Saved enum IDs changed: {name}'

    result = {
        'baseline_commit': baseline,
        'source_commit': git('rev-parse', 'HEAD').decode().strip(),
        'reviewed_upstream_commit': upstream,
        'ui_integration_upstream_commit': ui_upstream,
        'retained_ui_forms': len(forms),
        'unchanged_ui_forms': len(forms) - len(updated_forms),
        'upstream_updated_ui_forms': updated_forms,
        'retained_form_widgets': widget_count,
        'unchanged_apply_paths': list(apply_paths),
        'setting_definitions_match_baseline_plus_upstream': True,
        'original_setting_declarations_retained': len(original_settings),
        'upstream_updated_settings': updated_settings,
        'existing_enum_values_unchanged': True,
        'reviewed_setting_extensions': {'AspectRatio': ['R3_2', 'R2_1', 'R32_9', 'R32_10'],
            'NvidiaDlssQuality': ['Quality', 'Balanced', 'Performance', 'UltraPerformance']},
        'reviewed_adaptations': ['HDR On/Off key retained and migrated to Off/On/Auto',
            'HDR peak lower bound extended to zero; 200/300-nit defaults preserved',
            'New NVIDIA preset preserves game/default model selection',
            'Existing handheld performance overlay retained'],
        'forms_digest': hashlib.sha256('\n'.join(forms).encode()).hexdigest(),
        'note': 'Navigation/theme runtime retention is covered separately by standalone Qt tests.',
    }
    content = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(content, encoding='utf-8')
    print(content, end='')


if __name__ == '__main__':
    main()
