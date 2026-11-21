# SPDX-License-Identifier: GPL-3.0-or-later
import os
import subprocess
import sys

cases = [(None, None), ("1", None), ("1", "0"), ("1", "1"), ("0", "1"),
         (None, "1"), ("1", "true"), ("true", "1"), ("1", "01"), ("1", "")]
failures = 0
for frame, sites in cases:
    environment = os.environ.copy()
    for key, value in (("EDEN_DKB_FRAME_TRACE", frame), ("EDEN_DKB_GPU_SITES", sites)):
        environment.pop(key, None)
        if value is not None:
            environment[key] = value
    result = subprocess.run([sys.argv[1], str(int(frame == "1")), str(int(frame == "1" and sites == "1"))],
                            env=environment, text=True, capture_output=True, timeout=10)
    print(f"frame={frame!r} sites={sites!r}: {result.stdout.strip()}", flush=True)
    if result.returncode:
        failures += 1
        print(result.stderr, file=sys.stderr)
print(f"GPU-site matrix: {len(cases)} fresh processes, {failures} failed", flush=True)
sys.exit(bool(failures))
