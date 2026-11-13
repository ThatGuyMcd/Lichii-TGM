#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

import contextlib
import io
import json
from pathlib import Path
import tempfile
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frame_dump_index as index


class FrameDumpIndexTests(unittest.TestCase):
    def build_page(self, name="present00001", read_encoding="cp1252", write_encoding="cp1252"):
        with tempfile.TemporaryDirectory(prefix="fritchii-frame-index-") as directory:
            root = Path(directory)
            manifest = root / "manifest.jsonl"
            entries = [
                {"kind": "frame", "frame": 1, "seconds": 0.5},
                {"kind": "present", "name": name, "after_draws": 1},
                {"kind": "summary", "images": 1, "passes": 1, "draws": 1,
                 "dispatches": 0, "bytes": 0, "truncated": False},
            ]
            manifest.write_text("".join(json.dumps(e, ensure_ascii=False) + "\n" for e in entries),
                                encoding="utf-8")
            before = manifest.read_bytes()
            original_read = Path.read_text
            original_write = Path.write_text

            # Emulate a non-UTF-8 default locale on every test platform.
            def read_text(path, *args, **kwargs):
                kwargs.setdefault("encoding", read_encoding)
                return original_read(path, *args, **kwargs)

            def write_text(path, text, *args, **kwargs):
                kwargs.setdefault("encoding", write_encoding)
                return original_write(path, text, *args, **kwargs)

            with mock.patch.object(Path, "read_text", read_text), \
                    mock.patch.object(Path, "write_text", write_text), \
                    mock.patch("sys.argv", ["frame_dump_index.py", str(root)]), \
                    contextlib.redirect_stdout(io.StringIO()):
                index.main()

            self.assertEqual(manifest.read_bytes(), before)
            return (root / "index.html").read_bytes()

    def test_html_matches_its_declared_utf8_encoding_with_a_legacy_locale(self):
        page = self.build_page().decode("utf-8")
        self.assertIn("<meta charset=utf-8>", page)
        self.assertIn("frame 1 at 0.5 s · 1 images", page)
        self.assertIn("present · after draw 1", page)

    def test_utf8_manifest_text_is_preserved_with_a_legacy_reader(self):
        page = self.build_page(name="café", write_encoding="utf-8").decode("utf-8")
        self.assertIn('id="café"', page)
        self.assertIn('class="n">café</div>', page)
        self.assertNotIn("cafÃ©", page)


if __name__ == "__main__":
    unittest.main()
