#!/usr/bin/env python3
"""Inventory fork branding and intentionally retained upstream references."""

import collections
import json
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
paths = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z']).decode().split('\0')
references = []
for name in paths:
    if not name:
        continue
    path = root / name
    if not path.is_file():
        continue
    try:
        content = path.read_text(encoding='utf-8')
    except (UnicodeError, OSError):
        if 'lichii' in name.lower():
            references.append({'path': name, 'line': 0, 'category': 'artwork/profile/fixture filename'})
        continue
    for number, line in enumerate(content.splitlines(), 1):
        if 'lichii' not in line.lower():
            continue
        if name.startswith(('externals/', 'src/dynarmic/')):
            category = 'protected dependency or inherited third-party source'
        elif 'SPDX-' in line or 'copyright' in line.lower():
            category = 'original copyright attribution'
        elif name == '.github/workflows/release-binaries.yml':
            category = 'legacy upstream workflow, disabled on this fork'
        elif 'lichii-tgm' in line.lower() or 'lichii_tgm' in line.lower():
            category = 'fork branding'
        elif 'lichii.zortos.me' in line or 'X-Lichii-Client' in line:
            category = 'upstream compatibility-service API contract'
        elif re.search(r'https?://[^\s"<>]*lichii', line, re.I):
            category = 'upstream source/issue/provenance reference'
        elif name.startswith('src/tests/'):
            category = 'legacy compatibility/negative-test fixture'
        elif 'LICHII_' in line or 'lichii-cache' in line or 'lichii-2 ' in line:
            category = 'compatible build/configuration/cache identifier'
        elif name.startswith(('docs/', 'AGENTS.md')) or line.lstrip().startswith(('//', '#', '*')):
            category = 'inherited documentation/comment/provenance'
        else:
            category = 'inherited internal resource/tool/package identifier'
        references.append({'path': name, 'line': number, 'category': category,
                           'excerpt': line.strip()[:240]})
branding = (root / 'CMakeModules/Branding.cmake').read_text(encoding='utf-8')
report = {
    'release': re.search(r'set\(LICHII_TGM_RELEASE_TAG "([^"]+)"\)', branding)[1],
    'numeric_version': re.search(r'set\(LICHII_TGM_VERSION "([^"]+)"\)', branding)[1],
    'scope': 'Tracked project files, including hidden workflows; binary asset filenames included.',
    'counts': dict(collections.Counter(item['category'] for item in references)),
    'references': references,
}
data = json.dumps(report, indent=2, ensure_ascii=False) + '\n'
if len(sys.argv) == 2:
    Path(sys.argv[1]).write_text(data, encoding='utf-8')
    print(json.dumps({'references': len(references), 'counts': report['counts']}))
else:
    print(data, end='')
