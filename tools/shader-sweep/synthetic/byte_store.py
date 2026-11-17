# SPDX-License-Identifier: GPL-3.0-or-later
"""An independently assembled native STG.U8 regression program.

Instruction fields come from frontend/ampere/lower_to_maxwell.h, cases
0xb82 (LDC), 0x802 (MOV), 0x386 (STG), and 0x94d (EXIT). No captured
program, header, pipeline key, or environment is an input to this generator.
"""

import struct


POINTER_OFFSET = 0x120
BYTE_OFFSET = 3
STORE_VALUE = 0xA1B2C35A


def native_program():
    """LDC.64 R2,c0[0x120]; MOV R6,value; STG.U8 [R2+3],R6; EXIT."""
    predicate_true = 7 << 12
    return [
        # LDC.64: destination 16:23, index RZ 24:31, byte offset 38:53,
        # width 73:75 = 5. The load defines the complete pointer pair.
        [0xB82 | predicate_true | (2 << 16) | (255 << 24),
         POINTER_OFFSET << 6, 5 << 9, 0],
        # MOV immediate: literal word 1, byte mask 72:75 = 15.
        [0x802 | predicate_true | (6 << 16), STORE_VALUE, 15 << 8, 0],
        # STG.E.U8: data 32:39, signed byte displacement 40:63,
        # .E at bit 72 and width 73:75 = 0, strong GPU policy.
        [0x386 | predicate_true | (2 << 24),
         6 | (BYTE_OFFSET << 8), 0x0010A100, 0],
        [0x94D | predicate_true, 0, 7 << 23, 0],
    ]


def make_fixture():
    """Return source plus a synthetic replay environment; the runner lowers source."""
    header = [0] * 20
    # ProgramHeader: ordinary vertex header, with global-store access declared.
    header[0] = 1 | (1 << 10) | (1 << 16) | (1 << 26)
    header[2] = 1 << 24
    header[3] = 1 << 24
    header[4] = 1
    return {
        "name": "synthetic-byte-store",
        "provenance": "independently assembled from production instruction bitfields",
        "environment": {
            "native_code": native_program(),
            "code": ["0000000000000000"],
            "parameters": [0, 2, 0, 0, 0, 1, 0],
            "texture_types": [],
            "texture_formats": [],
            "cbuf_values": [[POINTER_OFFSET, 0x00800000], [POINTER_OFFSET + 4, 0]],
            "storage_pointers": [[POINTER_OFFSET, 1]],
            "replacements": [],
            "tail": struct.pack("<20I", *header).hex(),
        },
        "expected": {
            "storage_buffer_offsets": [POINTER_OFFSET],
            "storage_writes_u8": 1,
            "byte_offset": BYTE_OFFSET,
            "written_byte": STORE_VALUE & 0xFF,
        },
    }
