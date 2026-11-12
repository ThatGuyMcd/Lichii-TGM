# Lichii-tgm-v1.0.3

This revision brings in the reviewed changes from Lichii and useful OOT-related
fixes from Fritchii, while keeping the Forest Lantern UI, handheld layout, all
settings, aspect profiles and explicit shader preparation option.

## Updates

- Reviews 38 Lichii commits through `d5bb40ad4ca16b66e9c0961f823aff5f2a8c5c08`.
  Protected dependency sources are retained, so their CPU optimizations and the
  external FidelityFX shader change are not applied. The fork also retains its
  existing overlay, data paths and release packaging.
- Adds focused warm-up shader validation, pipeline-library capability checks and
  automatic precompile memory-pressure safeguards from the older shader PR series.
  The latest two main UI/LAN commits remain reviewed exclusions.
- Links nxformat 1.0.3 into the executable and adds the reviewed external NX
  update/DLC, patched RomFS dump, metadata editing and whole-gamecard fixes.
  The late upstream theme redesign is reviewed; Forest Lantern stays in use.
- Adds the reviewed read-only compute-buffer size bound, empty-submit handling,
  shader compiler changes, native HDR output and retained SPIR-V for rebuilding
  evicted graphics pipelines. Fork-specific GPL, optimized compilation, native
  preparation and OOT motion information remain in place.
- Adds focused fixes for emergency texture collection, preserving collected CPU
  writes, optional memory trimming under allocation failure, and shader operands
  that use high register numbers.
- Integrates Fritchii's native/UTF-8 mod paths, self-install protection and safe
  folder naming. Missing sources, overlapping install folders, occupied siblings
  and dangling links are checked before destructive operations.
- Retains all OOT aspect profiles and their Photo Mode compatibility. Texture
  Pool x2 was already present and is retained.
- Reuses compatible dynamic vertex-input draws while rejecting changed shader
  input types. A full change list retains every written range and restreams
  uniforms conservatively. This is a focused adaptation of
  [Lichii #1161](https://github.com/zortos293/Lichii/pull/1161), preserving this
  fork's native pipeline keys and motion handling.

## Settings and data

Existing Eden, portable and XDG locations stay in use. This update does not move
saves, keys, firmware, profiles or caches into a new application folder. The safer
staged import machinery is retained for explicit imports.
Existing configured profiles continue without a new import prompt, including
separate XDG config directories. Unfinished imports still require recovery;
the current profile and its aliases cannot be selected as import sources.

HDR Off/On/Auto replaces the old On/Off control, with its saved choice migrated and
the old key preserved for compatibility. Existing 200/300-nit defaults are retained;
zero is available for display-derived calibration. HDR10 and scRGB can be selected.

NVIDIA DLSS gains explicit quality and model choices on Windows and Linux. Existing
quality IDs, OOT anti-aliasing/render-scale choices, preset overrides and neural
rendering controls remain. The existing game/default model policy is retained
instead of silently selecting upstream's new M default. Live options are published
as one atomic snapshot for the renderer.

## Shader preparation

More Actions > Prepare game shaders remains a separate worker. Its pinned OOT
v131072 recipe, version/build checks, cache locks, cancellation and driver-cache
checkpoints remain. Pipeline records are counted separately from unique programs.
Missing facts remain rejected; preparation does not promise every possible runtime
pipeline or eliminate all stutter. Compiler changes invalidate stale prepared
translations through the existing source-revision stamp.

The new Build all cached shaders before game start setting concerns records saved
by earlier play sessions. It complements the explicit game-file preparation action.

## Validation and remaining work

Windows x86_64 and Linux x86_64 AppImage packages must pass the full test suites,
settings/UI retention checks, dependencies, package identity and checksum checks.
Detailed results are recorded in the build manifest accompanying the packages.

The local native-rendering performance candidate still needs matched gameplay
validation. Diagnostic tracing changes CPU command routing, so performance claims
must use separate measurements with detailed profiling and frame generation off.
Native 60 FPS and a twofold improvement have not been established.

Recent device-loss reports still need a controlled gameplay comparison. A Windows
GPU engine timeout means the display engine stopped responding; it does not by
itself identify a particular emulator defect. See [Microsoft's 0x141 description](https://learn.microsoft.com/windows-hardware/drivers/debugger/bug-check-0x141---video-engine-timeout-detected).
Test with stock GPU settings and otherwise unchanged game settings before
attributing a stability difference to this update. Builds and automated checks do
not establish that the GPU reset issue is fixed. No emulator or game is launched
automatically during validation.

Experimental lock-free reads, learned eviction that discards first-read data and
replacement preparation systems that drop recipe support are not included. See the
[integration audit](TGM-upstream-sync-2026-11-11.md) for source snapshots and dispositions.
