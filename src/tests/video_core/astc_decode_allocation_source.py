#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the production allocation orchestration, replacing only device boundaries."""
import argparse
import hashlib
import pathlib
import re

p = argparse.ArgumentParser()
p.add_argument('--source', type=pathlib.Path, required=True)
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--allocation-override', type=pathlib.Path)
a = p.parse_args()
raw = a.source.read_bytes()
source = raw.decode('utf-8')
start = source.index('    // The decode image holds RGBA8 for every layer at once:')
end = source.index('    // Encode every level into one buffer', start)
body = source[start:end]
constant = re.findall(r'(?:static )?constexpr u64 MaxFreeTranscodeBytes = [^;]+;', source)
if len(constant) != 1:
    raise RuntimeError('Expected exactly one production ASTC scratch budget')
if a.allocation_override:
    body = a.allocation_override.read_text(encoding='utf-8')
a.output.mkdir(parents=True, exist_ok=True)
(a.output / 'astc_decode_allocation.inc').write_text(body, encoding='utf-8')
(a.output / 'astc_decode_budget.inc').write_text(constant[0] + '\n', encoding='utf-8')
(a.output / 'astc_decode_source.sha256').write_text(hashlib.sha256(raw).hexdigest() + '\n', encoding='ascii')
