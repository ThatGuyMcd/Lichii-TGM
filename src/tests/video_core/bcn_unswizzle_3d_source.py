# SPDX-License-Identifier: GPL-3.0-or-later
"""Expose the actual BCn 3D push-constant bytes without a Vulkan device."""

from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = source.read_text(encoding="utf-8")
layout_start = text.index("struct alignas(16) BlockLinearUnswizzle3DPushConstants {")
layout_end = text.index("\n};", layout_start) + len("\n};")
chunk_start = text.index("void BlockLinearUnswizzle3DPass::UnswizzleChunk(")
constants_start = text.index("    BlockLinearUnswizzle3DPushConstants pc{};", chunk_start)
constants_end = text.index("\n    compute_pass_descriptor_queue.Acquire(", constants_start)

output.write_text(
    text[layout_start:layout_end]
    + "\n\nBlockLinearUnswizzle3DPushConstants MakePushConstants(\n"
    + "    const BlockLinearSwizzle3DParams& params, u32 blocks_x, u32 blocks_y,\n"
    + "    u32 z_start, u32 z_count) {\n"
    + text[constants_start:constants_end]
    + "\n    return pc;\n}\n",
    encoding="utf-8",
)
