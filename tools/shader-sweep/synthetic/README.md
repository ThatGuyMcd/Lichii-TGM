# Independent shader regressions

I've kept four small regression programs here, assembled from the compiler's
instruction bitfields. The Python source declares every instruction, header,
constant-buffer fact and pointer classification. The generators don't read game
files, captures, old shader payloads or pipeline keys.

| Program | Behaviour checked |
| --- | --- |
| `byte_store.py` | Native byte stores retain their width and unaligned byte address through lowering, storage routing and SPIR-V emission. |
| `unmapped_atomic.py` | An initially unmapped append counter keeps its storage binding; mapped and unmapped classifications emit the same program. |
| `global_memory_no_alias.py` | Coherent vector global-memory operations use valid scalar routing helpers when descriptor aliases are unavailable. |
| `geometry_passthrough.py` | Native and legacy-cache passthrough preserve Position/Generic6 without declaring unrelated vertex, tessellation or fragment inputs. |

The production fixes associated with these contracts are
[`921cd20e3a`](https://github.com/zortos293/Lichii/commit/921cd20e3a),
[`448fdaed95`](https://github.com/zortos293/Lichii/commit/448fdaed95),
[`af0b66e57f`](https://github.com/zortos293/Lichii/commit/af0b66e57fa127532b7dc397ef6ccfa9b5d89b57)
and [`8ff071d330`](https://github.com/zortos293/Lichii/commit/8ff071d330).
Original compiler contributions and their licences are retained. These new
fixtures and generators use GPL-3.0-or-later.

`generate_fixtures.py` renders the four JSON files in `../fixtures`. Use the
project's patch pipeline to apply regenerated files. To check reproducibility:

```sh
python -B tools/shader-sweep/generate_fixtures.py --check
```

The shader-sweep harness compiles native source and a serialized-cache roundtrip,
compares their output, and validates SPIR-V. It also checks the legacy geometry
record and the mapped-counter variant. The stage-translation tests check actual
IR operations/descriptors, passthrough decorations, worker concurrency, exception
propagation and shutdown behaviour.

The no-alias stage test uses 256-byte storage alignment as well as the CLI's
four-byte alignment. The latter already splits vectors for alignment reasons,
so it cannot alone expose the descriptor-alias regression.
