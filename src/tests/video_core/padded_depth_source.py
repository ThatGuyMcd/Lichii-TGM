# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract depth lookup and its actual dirty-flag refresh block for cache tests."""
import hashlib
from pathlib import Path
import sys

source, out = map(Path, sys.argv[1:])
raw = source.read_bytes()
text = raw.decode().replace("\r\n", "\n")
out.mkdir(parents=True, exist_ok=True)
def block(text, marker):
    assert text.count(marker) == 1
    start = text.index(marker)
    first = text.index("{", start)
    end, depth = first + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end], text[first + 1:end - 1]
_, lookup = block(text, "ImageViewId TextureCache<P>::FindDepthBuffer()")
refresh, _ = block(text, "        if (flags[Dirty::ZetaBuffer] || force")
_, update = block(text, "void TextureCache<P>::UpdateRenderTargets(bool is_clear)")
prefix = update.split("if (!flags[Dirty::RenderTargets])", 1)[0]
invalidate = "InvalidatePaddedDepthTargetForResolution();"
(out / "padded_depth_invalidation.inc").write_text(invalidate if invalidate in prefix else "")
(out / "padded_depth_lookup.inc").write_text(lookup)
reset = "        flags[Dirty::RenderTargets] = false;"
assert reset in text
(out / "padded_depth_refresh.inc").write_text(reset + "\n" + refresh)
(out / "padded_depth_presence.inc").write_text(
    "#define HAS_PADDED_DEPTH_GATE " + str(int("NarrowPaddedDepthTarget(" in text)) + "\n")
(out / "padded_depth_source.sha256").write_text(hashlib.sha256(raw).hexdigest() + "\n")
