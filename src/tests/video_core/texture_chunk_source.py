# SPDX-License-Identifier: GPL-3.0-or-later
"""Expose the exact production chunk-upload orchestration to CPU boundary tests."""
import hashlib
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_bytes()
text = source.decode("utf-8").replace("\r\n", "\n")
marker = "bool TextureCache<P>::UploadInChunks(Image& image) {"
assert text.count(marker) == 1
tail = text.split(marker, 1)[1]
assert "\n}\n" in tail
body = tail.split("\n}\n", 1)[0]
assert "template <" not in body
output = Path(sys.argv[2])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(body, encoding="utf-8")
output.with_suffix(".sha256").write_text(hashlib.sha256(source).hexdigest() + "\n")
