#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import json
import copy
from pathlib import Path
import struct
import subprocess
import tempfile


def serialize_environment(env):
    code = b"".join(struct.pack("<Q", int(word, 16)) for word in env["code"])
    fields = ("texture_types", "texture_formats", "cbuf_values", "storage_pointers", "replacements")
    data = struct.pack("<6Q7I", len(code), *(len(env[field]) for field in fields), *env["parameters"])
    data += code
    for field, fmt in zip(fields, ("<II", "<II", "<QI", "<QB", "<QI")):
        data += b"".join(struct.pack(fmt, *entry) for entry in env[field])
    return data + bytes.fromhex(env["tail"])


def cached_environment(source, packed):
    env = copy.deepcopy(source)
    env["code"] = [f"{word:016x}" for (word,) in struct.iter_unpack("<Q", packed)]
    env["parameters"][2] = 80 if env["parameters"][6] == 5 else 0
    env["parameters"][3] = 0
    env["parameters"][4] = len(packed) - 8
    return env


def legacy_prefix(packed, instructions):
    words = 10  # independently declared 80-byte Maxwell header
    for _ in range(instructions):
        if words % 4 == 0:
            words += 1
        words += 1
    words += 1  # environment terminator, before the native-address map
    if words * 8 >= len(packed):
        raise RuntimeError("Expected native-address map after the lowered program")
    return packed[:words * 8]


def main():
    parser = argparse.ArgumentParser(description="Compile independently assembled shader regressions")
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--fixture", default="*", help="fixture filename stem or glob")
    parser.add_argument("--spirv-val", default="spirv-val")
    parser.add_argument("--output-dir", type=Path, help="retain emitted modules and per-path results")
    args = parser.parse_args()
    validator = str(Path(args.spirv_val).resolve()) if Path(args.spirv_val).is_file() else args.spirv_val
    fixtures = sorted((Path(__file__).parent / "fixtures").glob(args.fixture + ".json"))
    if not fixtures:
        parser.error("no matching synthetic fixtures")
    results = []
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    for path in fixtures:
        fixture = json.loads(path.read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = root / "native.asm.bin"
            native.write_bytes(b"".join(struct.pack("<4I", *row) for row in fixture["environment"]["native_code"]))
            def run(label, environment, *, source=True, lower=False):
                env = root / (label + ".env")
                output = root / (label + (".bin" if lower else ".spv"))
                env.write_bytes(serialize_environment(environment))
                command = [str(args.compiler.resolve())]
                if source:
                    command += [str(native)]
                command += ["--environment", str(env), "--cache-version",
                            str(fixture["cache_version"]),
                            "--packed-code" if lower else "--output", str(output)]
                compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
                if compiled.returncode:
                    raise RuntimeError(f"{path.stem}/{label}: {compiled.stdout}{compiled.stderr}")
                diagnostic = json.loads(compiled.stdout)
                if diagnostic["status"] != "ok":
                    raise RuntimeError(f"{path.stem}/{label}: {diagnostic}")
                if not lower:
                    validated = subprocess.run([validator, "--target-env", "vulkan1.2",
                                                "--uniform-buffer-standard-layout", str(output)],
                                               capture_output=True, text=True, timeout=30)
                    if validated.returncode:
                        raise RuntimeError(f"{path.stem}/{label}: {validated.stdout}{validated.stderr}")
                    results.append({"fixture": path.stem, "path": label,
                                    "spirv_words": diagnostic["spirv_words"], "validated": True})
                    if args.output_dir:
                        (args.output_dir / (path.stem + "-" + label + ".spv")).write_bytes(output.read_bytes())
                return output.read_bytes(), diagnostic

            emitted, _ = run("native", fixture["environment"])
            packed, diagnostic = run("packed", fixture["environment"], lower=True)
            cached, _ = run("cached", cached_environment(fixture["environment"], packed), source=False)
            if cached != emitted:
                raise RuntimeError(f"{path.stem}: native/cached emission differs")
            if fixture["expected"].get("storage_atomics_iadd32"):
                mapped = copy.deepcopy(fixture["environment"])
                mapped["storage_pointers"][0][1] = 1
                if run("mapped", mapped)[0] != emitted:
                    raise RuntimeError("Mapped/unmapped counter routing differs")
            if fixture["expected"].get("geometry_passthrough"):
                legacy = legacy_prefix(packed, diagnostic["lowered_words"])
                run("legacy", cached_environment(fixture["environment"], legacy), source=False)
            print(f"PASS {path.stem}: native/cached and applicable legacy/mapping paths")
    if args.output_dir:
        (args.output_dir / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
