# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the production decision made after translating cached graphics."""

from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = source.read_text(encoding="utf-8")
method = text.index("PipelineCache::TranslateCachedGraphics(")
translate_end = text.index("&frozen_pointer_word)};", method) + len("&frozen_pointer_word)};")
end = text.index("\n}", translate_end)
output.write_text(text[translate_end:end].lstrip("\n") + "\n", encoding="utf-8")
