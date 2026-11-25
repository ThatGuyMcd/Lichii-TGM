# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the scene save, slot lookup and effects mark to run with synthetic views."""
from pathlib import Path
import sys


def section(text, start, end):
    assert text.count(start) == 1, start
    content, found, _ = text.split(start, 1)[1].partition(end)
    assert found, end
    return content


cache, rasterizer, out = map(Path, sys.argv[1:])
cache_text = cache.read_text(encoding="utf-8")
rasterizer_text = rasterizer.read_text(encoding="utf-8")
generation = "[[nodiscard]] u32 ImageViewGeneration("
bump = "void BumpImageViewGeneration("
lookup = "[[nodiscard]] const ImageView* GetImageViewIfCurrent("


def member(marker):
    return marker + section(cache_text, marker, "\n    }") + "\n    }\n"


save = section(rasterizer_text, "scene.range == VideoCommon::SubresourceRange{}) {", "\n            }")
mark = section(rasterizer_text, "    if (marking_effects) {", "\n    const auto* const replacement")
assert save.count("SaveEffectsBase(") == 1 and mark.count("MarkEffects(") == 1
out.mkdir(parents=True, exist_ok=True)
(out / "oot_effects_view_cache.inc").write_text(
    member(generation) + member(bump) + (member(lookup) if lookup in cache_text else ""),
    encoding="utf-8",
)
(out / "oot_effects_view_save.inc").write_text(save, encoding="utf-8")
(out / "oot_effects_view_mark.inc").write_text("    if (marking_effects) {" + mark, encoding="utf-8")
