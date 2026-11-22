# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the complete production traversal, with a source fingerprint."""
import hashlib
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_bytes()
text = source.decode().replace("\r\n", "\n")
marker = "    void IterateCache(VAddr addr, std::size_t size, Func&& func) {"
assert text.count(marker) == 1
body = text.split(marker, 1)[1].split("\n    }\n", 1)[0]
assert "cached_queries" in body and "template <" not in body
output = Path(sys.argv[2])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(body, encoding="utf-8")
output.with_suffix(".sha256").write_text(hashlib.sha256(source).hexdigest() + "\n")
