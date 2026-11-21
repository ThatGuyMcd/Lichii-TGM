# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract production scheduler orchestration verbatim for a device-free contract test."""
import hashlib
import json
from pathlib import Path
import re
import sys

root, output = map(Path, sys.argv[1:])
header_path = root / "src/video_core/renderer_vulkan/vk_scheduler.h"
source_path = header_path.with_suffix(".cpp")
header, source = header_path.read_text(), source_path.read_text()


def unique(text, anchor):
    if text.count(anchor) != 1:
        raise ValueError("Scheduler extraction anchor changed: " + anchor)
    return text.index(anchor)


start = unique(header, "    template <typename T>\n    void RecordAs(")
end = unique(header, "\n    struct State {")
assert end > start
record = header[start:end]
start = unique(source, "    const auto physical = device.GetPhysical();\n    trace_timestamp_bits =")
end = unique(source, "    if (VideoCommon::ReadTrace::Enabled()) {")
assert end > start
constructor = source[start:end]
constants = []
for name in ("TraceMaxSubmissions", "TraceMaxWork", "TraceMaxSites"):
    matches = re.findall(r"    static constexpr u32 " + name + r" = [0-9]+;", header)
    if len(matches) != 1:
        raise ValueError("Scheduler constant changed: " + name)
    constants.extend(matches)
output.mkdir(parents=True, exist_ok=True)
files = {"gpu_site_record.inc": record, "gpu_site_constructor.inc": constructor,
         "gpu_site_constants.inc": "\n".join(constants) + "\n"}
for name, text in files.items():
    (output / name).write_text(text)
receipt = {"source": {str(p.relative_to(root)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in (header_path, source_path)},
           "extracted": {n: hashlib.sha256((output / n).read_bytes()).hexdigest() for n in files}}
(output / "gpu_site_source.json").write_text(json.dumps(receipt, indent=2) + "\n")
