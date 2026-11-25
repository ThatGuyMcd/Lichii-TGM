# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract production Vulkan commands for the native blit/resolve dispatch test."""

from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = source.read_text(encoding="utf-8")


def function(signature):
    start = text.index(signature)
    end = text.index("\n}", start) + len("\n}")
    return text[start:end]


blit = function("void TextureCacheRuntime::BlitImage(")
start = blit.index("        const std::array read_barriers{")
end = blit.index("\n    });", start)
commands = blit[start:end]
if not all(f"cmdbuf.{call}(" in commands for call in ("BlitImage", "ResolveImage", "PipelineBarrier")):
    raise ValueError("Missing native texture transfer command")

output.write_text(
    function("[[nodiscard]] VkImageBlit MakeImageBlit(")
    + "\n\n"
    + function("[[nodiscard]] VkImageResolve MakeImageResolve(")
    + "\n\n"
    + "void RecordTransfer(vk::CommandBuffer cmdbuf, Fermi2D::Filter filter,\n"
    + "                    Region2D dst_region, Region2D src_region,\n"
    + "                    VkImage dst_image, VkImage src_image,\n"
    + "                    VkImageSubresourceLayers dst_layers,\n"
    + "                    VkImageSubresourceLayers src_layers,\n"
    + "                    VkImageAspectFlags aspect_mask, bool is_resolve) {\n"
    + "    const VkImageLayout dst_layout = BlitDestinationLayout(src_image == dst_image);\n"
    + commands
    + "\n}\n",
    encoding="utf-8",
)
