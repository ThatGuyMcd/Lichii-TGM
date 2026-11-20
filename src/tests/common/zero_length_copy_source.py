# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the production copy paths for the dependency-free sanitizer regression."""
import argparse
import hashlib
import json
from pathlib import Path


def generate(source_root: Path, output_dir: Path) -> None:
    paths = ["src/common/scratch_buffer.h", "src/core/file_sys/nup_writer.cpp"]
    sources = [(source_root / path).read_bytes() for path in paths]
    scratch, writer = [source.decode("utf-8").replace("\r\n", "\n") for source in sources]
    signature = "void PutBytes(std::vector<u8>& buffer, size_t offset, std::span<const u8> bytes) {"
    if writer.count(signature) != 1:
        raise RuntimeError("Expected exactly one production PutBytes definition")
    start = writer.index(signature)
    end = writer.index("\n}", start) + 2
    function = writer[start:end]
    # Fail closed if an intervening declaration or a changed brace layout defeats extraction.
    if function.count("{") != function.count("}"):
        raise RuntimeError("Unbalanced PutBytes extraction")
    output_dir.mkdir(parents=True, exist_ok=True)
    header = ('// Generated from production source; do not edit.\n'
              '#include <cstdint>\n#include <span>\n#include <vector>\n'
              '#line 1 "src/common/scratch_buffer.h"\n' + scratch +
              '\nnamespace CopyRegression {\nusing u8 = std::uint8_t;\n' +
              f'#line {writer[:start].count(chr(10)) + 1} "src/core/file_sys/nup_writer.cpp"\n' +
              function + '\n}\n')
    (output_dir / "zero_length_copy_source.h").write_text(header, encoding="utf-8")
    (output_dir / "zero_length_copy_source.json").write_text(json.dumps({
        "source_root": str(source_root.resolve()),
        "sha256": {path: hashlib.sha256(source).hexdigest()
                   for path, source in zip(paths, sources)},
        "extracted_put_bytes_sha256": hashlib.sha256(function.encode()).hexdigest(),
        "scope": "Entire ScratchBuffer header and verbatim private NUP PutBytes only",
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    generate(args.source_root, args.output_dir)
