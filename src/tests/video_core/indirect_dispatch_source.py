# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract complete production buffer lookup and indirect dispatch orchestration."""
import hashlib
import json
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
hashes = {}
def read(name):
    raw = (root / name).read_bytes()
    hashes[name] = hashlib.sha256(raw).hexdigest()
    return raw.decode().replace("\r\n", "\n")

text = read("src/video_core/buffer_cache/buffer_cache.h")
start = text.index("BufferCache<P>::ObtainBuffer(GPUVAddr gpu_addr,")
body = text[text.index("{", start) + 1:].split("\n}\n", 1)[0]
(output / "indirect_obtain_buffer.inc").write_text(body)
text = read("src/video_core/buffer_cache/indirect_buffer.h")
body = text.split("auto ObtainIndirectBuffer(Cache& cache, GPUVAddr address) {", 1)[1].split("\n}\n", 1)[0]
(output / "indirect_obtain_arguments.inc").write_text(body)
text = read("src/video_core/renderer_vulkan/vk_rasterizer.cpp")
marker = "    if (indirect_address) {\n        // DispatchIndirect\n"
assert text.count(marker) == 1
body = marker + text.split(marker, 1)[1].split("    const std::array<u32, 3> dim", 1)[0]
assert body.rstrip().endswith("}") and "SubmitNx2ListCopies" in body
(output / "indirect_dispatch.inc").write_text(body)
(output / "source-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n")
