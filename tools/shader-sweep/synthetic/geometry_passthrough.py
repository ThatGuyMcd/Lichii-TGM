# SPDX-License-Identifier: GPL-3.0-or-later
"""Sparse geometry passthrough with deliberately irrelevant stage-specific mask bits.

Sources: Ampere LowerToMaxwell OUT/EXIT cases, ProgramHeader, IR::Attribute,
and the DefineInputs fix 8ff071d330. No captured payload is an input.
"""

from .common import environment, fixture, native


GENERIC_LOCATION = 6
POSITION = tuple(range(28, 32))
GENERIC = tuple(range(32 + GENERIC_LOCATION * 4, 36 + GENERIC_LOCATION * 4))
# TessCoord U/V, VertexId and FrontFace may appear in a hardware skip mask;
# they must not turn into inputs of a geometry shader without actual loads.
IRRELEVANT_STAGE_ATTRIBUTES = (188, 189, 191, 255)


def native_program():
    return [
        # OUT finalization: RZ destination/stream, R23 output handle, neither emit nor cut.
        native(0x324, (16, 8, 255), (24, 8, 23), (32, 8, 255), (78, 2, 0)),
        native(0x94D, (87, 3, 7)),
    ]


def make_fixture():
    header = [0] * 20
    header[0] = 1 | (4 << 10) | (1 << 24)  # geometry stage and passthrough
    header[2] = 1 << 24  # one invocation
    header[3] = 1 << 24  # point list, matching the standalone compiler's input default
    header[4] = 1        # one output vertex
    skip_mask = [0xFFFFFFFF] * 8
    for attribute in POSITION + GENERIC + IRRELEVANT_STAGE_ATTRIBUTES:
        skip_mask[attribute // 32] &= ~(1 << (attribute % 32))
    return fixture(
        "synthetic-geometry-passthrough",
        "Native finalization passes Position and Generic6 through without declaring "
        "stage-specific builtins from the inverted skip mask.",
        environment(native_program(), 3, header=header, passthrough_mask=skip_mask),
        {
            "geometry_passthrough": True,
            "passthrough_attributes": list(POSITION + GENERIC + IRRELEVANT_STAGE_ATTRIBUTES),
            "generic_location": GENERIC_LOCATION,
            "spirv_builtin_allowlist": [0],  # Position
            "passthrough_decorations": 2,
            "geometry_emit_count": 0,
            "geometry_end_count": 0,
            "regression_fix": "8ff071d330",
        },
    )
