# Lichii-tgm-v1.0.3

I've brought our fork up to speed with the useful commits and PRs from Lichii and
Fritchii, keeping OOT Remake as the priority. Our Forest Lantern UI, handheld layout
and every existing setting are still here. I've left the experimental changes for
later.

This release includes Windows x86_64 and Linux x86_64 AppImage builds from the same
source commit. The accompanying manifest records the exact source and validation.

## Changes

- Shutdown fixes address stop-time guest PANIC ordering and the Windows closing
  deadlock. Windows also gains local crash context and minidump capture.
- Device-memory cleanup releases contained mappings correctly. Freed SMMU blocks
  can be reused, with pressure handling that returns retained blocks when needed.
- Self-contained NUP and tree-NX games keep their executable and game-data versions
  together. Installed NCA updates and user mods retain their existing behavior.
- Renderer fixes cover mesh-emulation buffer lifetime, combined task/mesh descriptor
  limits, uniform-write notifications, pointer-word recapture, query cleanup and
  fence wake/retirement handling. Existing motion, report ownership and shader-cache
  safeguards remain.
- Shader translation gains lazy SSA phis and unused-identity cleanup. Geometry
  output, sparse-image and same-image blit checks improve portable Vulkan behavior.
- Interrupted shader-cache boot loading can recover without repeating the same
  failed boot prebuild. Existing cache contents and explicit preparation remain.
- More Actions > Prepare game shaders retains its separate worker, pinned OOT
  recipe, version/build checks, cache locks, cancellation and driver checkpoints.
  Compressed `.zst` shader archives are recognized too. Missing information still
  prevents incomplete preparation from being reported as complete.
- The custom clock keeps both its date and numeric offset controls, including
  signed 64-bit offsets and per-game restore behavior. Recommended settings can
  back up and restore the relevant per-game overrides while retaining other choices.
- Audio adds a no-microphone option that captures silence without opening a host
  microphone. Audio output and existing input-device choices remain available.
- Hardware GPUs are preferred over software renderers. Linux DLSS can discover
  versioned runtime libraries through the existing backend and search paths.
- Frame-dump reports use UTF-8 explicitly. Bounded driver-cache diagnostics report
  actual file ages and identify incomplete scans.

## UI, settings and data

Forest Lantern dark/light themes, selected-game artwork, touch spacing, full-screen
settings and the handheld layout are retained. Saves, keys, firmware, profiles and
Eden/portable/XDG data paths are preserved. Existing OOT aspect profiles and Photo
Mode compatibility remain.

This release also includes the reviewed v1.0.3 work already prepared locally:
nxformat 1.0.3 integration, retained SPIR-V, warm-up capability/memory guards,
safe mod installation/naming, all-channel deleted-buffer cleanup and the 64-bit
storage-binding correction.

## Validation

Both packages must pass the complete applicable test suites, Linux permission
checks without administrator privileges, settings/UI retention, protected-source
identity, dependencies, package versions, worker execution, standalone AppImage Qt
launcher and checksum checks. Exact results are in the release manifest.

Gameplay performance has not been benchmarked for this integration. The earlier
native-60 investigation is deferred; these automated checks do not establish that
GPU timeouts or every location-dependent game issue are resolved. Frame-generation
configuration and experimental defaults are retained without forced changes.

Source attribution, selected subsets and exclusions are documented in
[the integration notes](TGM-upstream-sync-2026-11-13.md).
