#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a test boundary from verbatim production boot/flush orchestration."""
import argparse
from pathlib import Path
import re


def extract(source, output):
    text = source.read_text(encoding='utf-8')
    start = '    if (build_at_boot) {\n        PreparePrecompiled('
    end = '    if (state.statistics) {'
    assert text.count(start) == 1, 'Boot orchestration boundary changed'
    first = text.index(start)
    last = text.index(end, first)
    boot = text[first:last]
    assert boot.count('SerializeVulkanPipelineCache(') == 1
    start = 'void PipelineCache::QueueVulkanPipelineCacheFlush() {'
    end = 'void PipelineCache::QueueVulkanPipelineCacheSave() {'
    assert text.count(start) == text.count(end) == 1
    flush = text.split(start, 1)[1].split(end, 1)[0].rstrip()
    assert flush.endswith('}')
    flush = flush[:-1]
    constants = []
    for name in ('VULKAN_CACHE_FLUSH_PIPELINES', 'VULKAN_CACHE_FLUSH_MIN_SECONDS'):
        matches = re.findall(r'^constexpr size_t '+name+r' = [0-9]+;', text, re.MULTILINE)
        assert len(matches) == 1
        constants.append(matches[0])
    output.mkdir(parents=True, exist_ok=True)
    header = ('// Generated verbatim from vk_pipeline_cache.cpp; do not edit.\n'
        '// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project\n'
        '// SPDX-License-Identifier: GPL-3.0-or-later\n'
        '// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project\n'
        '// SPDX-License-Identifier: GPL-2.0-or-later\n')
    for name, body in (('tail', boot), ('flush', flush), ('constants', '\n'.join(constants))):
        (output/f'boot_driver_cache_{name}.inc').write_text(header+body+'\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    extract(args.source, args.output)
