# SPDX-License-Identifier: GPL-3.0-or-later
"""Small encoders for independently authored shader regression fixtures."""

import struct


CACHE_VERSION = 54


def native(opcode, *fields):
    """Encode an Ampere opcode and (offset, width, value) fields, with PT guard."""
    if not 0 <= opcode < (1 << 12):
        raise ValueError("opcode must fit twelve bits")
    encoded = opcode | (7 << 12)
    for offset, width, value in fields:
        if offset < 0 or width <= 0 or offset + width > 128 or not 0 <= value < (1 << width):
            raise ValueError(f"invalid instruction field {(offset, width, value)}")
        mask = ((1 << width) - 1) << offset
        encoded = (encoded & ~mask) | (value << offset)
    return [(encoded >> shift) & 0xFFFFFFFF for shift in range(0, 128, 32)]


def environment(native_code, stage, *, header=None, passthrough_mask=None,
                texture_types=(), texture_formats=(), cbuf_values=(), storage_pointers=(),
                workgroup=(1, 1, 1), shared_memory=0, local_memory=0, texture_bound=2,
                viewport_transform=1):
    """Build the replay ABI from declared values, without a captured input record."""
    if stage == 5:
        tail = struct.pack("<4I", *workgroup, shared_memory)
    else:
        if header is None:
            header = [0] * 20
            header[0] = 1 | ((stage + 1) << 10)
        tail = struct.pack("<20I", *header)
        if stage == 3:
            if passthrough_mask is None:
                raise ValueError("geometry requires an explicit passthrough skip mask")
            tail += struct.pack("<8I", *passthrough_mask)
    return {
        "native_code": native_code,
        # A valid inert serialized record. Both consumers replace this with production lowering.
        "code": ["0000000000000000"],
        "parameters": [local_memory, texture_bound, 0, 0, 0, viewport_transform, stage],
        "texture_types": [list(entry) for entry in texture_types],
        "texture_formats": [list(entry) for entry in texture_formats],
        "cbuf_values": [list(entry) for entry in cbuf_values],
        "storage_pointers": [list(entry) for entry in storage_pointers],
        "replacements": [],
        "tail": tail.hex(),
    }


def fixture(name, description, environment, expected=None):
    return {
        "name": name,
        "cache_version": CACHE_VERSION,
        "provenance": "independently authored from production instruction bitfields",
        "description": description,
        "environment": environment,
        "expected": expected or {},
    }
