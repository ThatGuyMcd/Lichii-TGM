# SPDX-License-Identifier: GPL-3.0-or-later
"""Expose the unchanged frame/collector wiring to the real-cache regression fixture."""
import hashlib
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_bytes()
text = source.decode("utf-8")
start = "    bool out_of_memory = false;"
end = "    sentenced_images.Tick();"
assert text.count(start) == text.count(end) == 1
body = text.split(start, 1)[1].split(end, 1)[0]
output = Path(sys.argv[2])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(start + body, encoding="utf-8")
output.with_suffix(".sha256").write_text(hashlib.sha256(source).hexdigest() + "\n")
