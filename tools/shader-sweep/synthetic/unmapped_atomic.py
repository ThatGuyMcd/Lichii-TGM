# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent native append-counter routing regression.

The contract is the direct pointer case in CollectAtomicPointers and
CollectStorageBuffers: an atomic pointer keeps a storage binding even when
unmapped during compilation, including later stores based on its return.
All instructions and environment values are assembled here from field names
and constraints in frontend/ampere/lower_to_maxwell.h.
"""

import struct


COUNTER_OFFSET = 0x140
RESULT_OFFSET = 0x160
RESERVE_BYTES = 4
RECORD_OFFSET = 16


def native_program():
    """Reserve four bytes, publish the old counter, and write the reserved record."""
    predicate_true = 7 << 12

    def load_pointer(destination, offset):
        # LDC.64 from a direct c0 byte offset; index register is RZ.
        return [0xB82 | predicate_true | (destination << 16) | (255 << 24),
                offset << 6, 5 << 9, 0]

    def store(address, value, offset=0):
        # STG.E.B32: width bits 73:75 = 4, register address 24:31.
        return [0x386 | predicate_true | (address << 24),
                value | (offset << 8), 0x0010A100 | (4 << 9), 0]

    return [
        load_pointer(2, COUNTER_OFFSET),
        load_pointer(4, RESULT_OFFSET),
        [0x802 | predicate_true | (6 << 16), RESERVE_BYTES, 15 << 8, 0],
        # ATOMG.E.ADD.STRONG.GPU R8,[R2],R6: ADD bits 87:90 = 0.
        [0x3A8 | predicate_true | (8 << 16) | (2 << 24), 6, 0x001EE100, 0],
        store(4, 8),
        # IADD3 R2,R2,R8,RZ, no carry predicates: third source 64:71 = RZ,
        # carry inputs and outputs 77:90 = all-ones (discarded).
        [0x210 | predicate_true | (2 << 16) | (2 << 24),
         8, 255 | (0x7F << 13) | (0x7F << 20), 0],
        store(2, 6, RECORD_OFFSET),
        [0x94D | predicate_true, 0, 7 << 23, 0],
    ]


def make_fixture(mapped=False):
    """Return identical native source for mapped and initially unmapped counters."""
    return {
        "name": "synthetic-unmapped-atomic-counter",
        "provenance": "independently assembled from production instruction bitfields",
        "environment": {
            "native_code": native_program(),
            "code": ["0000000000000000"],
            "parameters": [0, 2, 80, 0, 0, 1, 5],
            "texture_types": [],
            "texture_formats": [],
            "cbuf_values": [[COUNTER_OFFSET, 0x00900000], [COUNTER_OFFSET + 4, 0],
                            [RESULT_OFFSET, 0x00A00000], [RESULT_OFFSET + 4, 0]],
            "storage_pointers": [[COUNTER_OFFSET, int(mapped)], [RESULT_OFFSET, 1]],
            "replacements": [],
            "tail": struct.pack("<4I", 8, 1, 1, 0).hex(),
        },
        "expected": {
            "storage_buffer_offsets": [COUNTER_OFFSET, RESULT_OFFSET],
            "storage_atomics_iadd32": 1,
            "global_atomics_iadd32": 0,
            "storage_writes_u32": 2,
            "global_writes_u32": 0,
            "reserve_bytes": RESERVE_BYTES,
            "record_offset": RECORD_OFFSET,
        },
    }
