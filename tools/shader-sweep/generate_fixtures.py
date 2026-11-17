#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Materialize only independently authored fixtures; never reads existing fixture files."""

import argparse
import importlib
import json
from pathlib import Path

from synthetic.common import CACHE_VERSION


MODULES = ("byte_store", "unmapped_atomic", "global_memory_no_alias", "geometry_passthrough")


def fixtures():
    for module in MODULES:
        result = importlib.import_module(f"synthetic.{module}").make_fixture()
        result.setdefault("cache_version", CACHE_VERSION)
        yield result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).parent / "fixtures")
    parser.add_argument("--check", action="store_true", help="verify committed fixtures without rewriting them")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for result in fixtures():
        path = args.output_dir / (result["name"] + ".json")
        rendered = json.dumps(result, indent=2) + "\n"
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != rendered:
                raise SystemExit(f"Generated fixture differs: {path.name}")
        else:
            path.write_text(rendered, encoding="utf-8")
        print(path.name)


if __name__ == "__main__":
    main()
