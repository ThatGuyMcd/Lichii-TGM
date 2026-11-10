# Upstream synchronization — 10 November 2026

Integrates all twenty-two commits after `d2eaae4f3abca636b030b726f107bace06f61079`
through Lichii `nx2` at `7dddff2d0e4b6f2802c12226c9d954771fe72113`.
The fork's prior local source is `07390bea197045eb156655d9a105d8d901b381be`;
the integration retains its Forest Lantern themes, handheld controls, selected-game
covers, full-screen settings and prior OOT integrations. Product revision remains
**Lichii-tgm-v1.0.2**. Synchronizing source does not publish a GitHub release.

## Incoming changes

| Commit | Upstream PR | Change |
| --- | --- | --- |
| `314e1d9469` | #1196 | ADPCM sign correction and OOT ocarina audio regressions; the fork already carries the corrected decoder. |
| `536853e71d` | #1197 | RomFS-only substitute for damaged compressed NUP members. |
| `467e772417` | #1200 | Optional recommended settings offer with backup and restore. |
| `610bd08731` | #1154 | MSVC graphics-pipeline variable-name correction. |
| `bcb37c49f7` | #1199 | Unmap every GPU alias of destroyed native memory. |
| `ddd19a170b` | #1210 | Bound Windows texture-cache thresholds by remaining system commit. |
| `093e264c8a` | #1212 | Avoid shadowing the global settings object. |
| `9d90f25bfe` | #1236 | Release destroyed GPU backing and refresh reused device ranges; targets stale OOT meshes after area changes. |
| `e8be28e5d6` | #1231 | Extend read-only native compute buffer bindings to their continuous mapping. |
| `7fcfb64123` | #1246 | Keep a dispatch's barrier with its command; already present in the fork, including native-list dispatches. |
| `4d7412ac7e` | #1245 | In-order upload batching; retain the fork's richer implementation and add per-region bounds and overlap checks. |
| `2aa201ac56` | #941 | Treat a completed DLSS evaluation's VRAM-budget warning as success; already present in the fork's shared NVIDIA interface. |
| `fb6a2689fa` | #1168 | Avoid waiting for unstarted time services when applying settings during loading. |
| `75b11063a1` | #1128 | OOT upscaler and pipeline-builder shadowing corrections; already present in the fork. |
| `76f144b530` | #972 | Shut down initialized kernel state when no loader is available. |
| `9703d5e29d` | #1108 | Portable IPSwitch escape character; already present in the fork. |
| `7d727554ae` | #1126 | Make MSVC diagnostic severity follow ENABLE_WERROR; already present in the fork. |
| `80197189dd` | #959 | Retry over-budget Vulkan buffer allocations; already present in the fork. |
| `f5edb3578d` | #784 | Block-linear 16-byte swizzling and regressions; retain the fork's existing implementation and parallel decoder. |
| `fd1dbeccab` | #850 | Size sysmix queues for the host callback; retain the fork's tested helper and add callback-size diagnostics. |
| `017c82c291` | #853 | Copy cached 3D surfaces one depth slice at a time for 2D-engine blits. |
| `7dddff2d0e` | #796 | Generated-frame FPS display; already present in the fork's status bar and overlay. |

## Merge decisions

Both the fork's available-physical-memory helper and upstream's system-commit
helper are retained. The texture cache keeps the fork's integrated-GPU heap
adjustment; the new host-commit bound is applied afterwards so that adjustment
cannot bypass it. Smaller explicit thresholds remain smaller, and unknown host
commit limits leave the existing thresholds intact. Regression cases cover this
combination. The initial-texture-content and host-memory headers both remain in
the build list.

The NUP workaround can restore rendering with a **different texture** when a
compressed member's header is damaged. It does not recover the original artwork.
Literal file reads, integrity checking, archive extraction and repacking preserve
the stored bytes. A clean game/update dump remains the way to restore original
data. Earlier release notes describe their historical behaviour.

Recommended settings require an explicit choice, retain a restore backup, and do
not change per-game settings, language, region, resolution or graphics backend.
Their Help actions remain accessible through the handheld Menu. Existing
configuration application paths are retained.

The second upstream batch ports several fixes the fork already carries. The merge
keeps its source-location tracing, reordered uploads, native-list dispatch barriers
and shared Windows/Linux NVIDIA upscaler interface. A duplicated DLSS helper and
obsolete header introduced by automatic merging are removed. The new per-region
upload check is shared with regression tests: one 66-region span must split into
64/1/1 batches, keeping an overlapping final write in a later batch; dependent
source buffers also flush earlier writes first.

The final upstream batch retains the fork's parallel swizzle path and its guarded
sysmix queue sizing and generated-frame counters. It adds the cached-volume
slice-copy path and callback-size diagnostics. Handheld toolbar and status-bar
controls remain in place.

Protected dependency/submodule and recompiled-source directories are unchanged.
No emulator is started or stopped for verification. The NVIDIA/iCUE blue screen
is a separate confirmed kernel-driver failure; this merge does not establish a fix
for it.

## Verification procedure

Before updating the fork, build Windows GUI/CLI/updater and Linux GUI/CLI plus
AppImage from the same committed source, run all nine CTest groups on both,
check UI retention against the baseline plus this reviewed upstream snapshot,
verify bundled dependencies and package hashes, and run the isolated Qt AppImage
launcher probe. Gameplay and physical handheld testing remain owner checks.
