# SPDX-License-Identifier: GPL-3.0-or-later
"""Hand-authored native program exercising routed vectors without descriptor aliases.

The encoding fields come from frontend/ampere/lower_to_maxwell.h. The contract
comes from the synthetic global_memory_coherence/global_atomic_routing tests
and the production fix af0b66e57fa127532b7dc397ef6ccfa9b5d89b57.
"""

from .common import environment, fixture, native


def make_fixture():
    # R0:R1 and R2:R3 are independent bound pointers. Select both halves with
    # the same lane predicate so the storage pass must retain both candidates.
    program = [
        native(0xB82, (16, 8, 0), (24, 8, 255), (38, 16, 0x80), (73, 3, 5)),
        native(0xB82, (16, 8, 2), (24, 8, 255), (38, 16, 0xA0), (73, 3, 5)),
        native(0x919, (16, 8, 4), (72, 8, 33)),  # S2R R4, SR_TID_X
        native(0x20C, (24, 8, 4), (32, 8, 255), (76, 3, 5),
               (81, 3, 0), (84, 3, 7), (87, 3, 7)),  # ISETP.NE.AND P0, PT
        native(0x207, (16, 8, 0), (24, 8, 0), (32, 8, 2), (87, 3, 0)),
        native(0x207, (16, 8, 1), (24, 8, 1), (32, 8, 3), (87, 3, 0)),
        # Scoped descriptor addressing, accepted by the native decoder as a
        # coherent 128-bit load through R0:R1; the descriptor uniform is unused.
        native(0x981, (16, 8, 8), (24, 8, 0), (64, 32, 0x0C1EBD00)),
        native(0x386, (24, 8, 0), (32, 8, 8), (40, 24, 16),
               (64, 32, 0x0010AD00)),  # STG.E.128 [R0:R1 + 16], R8:R11
        native(0x94D, (87, 4, 7)),
    ]
    env = environment(
        program, stage=5, workgroup=(4, 1, 1),
        cbuf_values=((0x80, 0x2000), (0x84, 1), (0xA0, 0x4000), (0xA4, 1)),
        storage_pointers=((0x80, 1), (0x84, 0), (0xA0, 1), (0xA4, 0)),
    )
    return fixture(
        "synthetic-global-memory-no-alias",
        "Lane-selected native buffer pointers retain coherent global vector loads and stores; "
        "a host without descriptor aliases assembles/splits them through scalar routing helpers.",
        env,
        expected={
            "uses_global_memory": True,
            "native_global_bounds": True,
            "requires_coherent_global_memory": True,
            "storage_buffer_count": 2,
            "ir_opcodes": ["LoadGlobal128", "WriteGlobal128"],
            "no_descriptor_aliases": True,
            "scalarized_global_vectors": True,
        },
    )
