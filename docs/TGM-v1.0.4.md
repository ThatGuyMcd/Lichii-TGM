# Lichii-tgm-v1.0.4

I've brought together the OOT Remake fixes and supporting renderer, shader-cache
and loader changes from this round of work. Our Forest Lantern UI, selected-game
artwork, handheld layout, full-screen settings and all of the existing settings
are still here. Saves, profiles, data paths, Photo Mode and the aspect-ratio
add-ons keep their existing behaviour.

Windows and Linux AppImage versions come from the same source commit.

## OOT and rendering

- I've corrected the OOT patch-storage reservation across both NSO loading passes,
  and retained the movie timeline, Scarecrow recording, moving-platform, water,
  sound, step-counter and Gohma timing fixes alongside the existing FPS Unlock.
- Native upload tracking is retired after dispatch. Stream-buffer wrap, mapping
  runs, staging-buffer ownership and descriptor preparation now preserve resources
  through their final recorded use. Texture cleanup retains pending CPU writes.
- Buffer-binding queries reuse mapping spans, and pages without cached images
  avoid unnecessary texture-cache locking. Contended CPU writes are gathered
  before command lists consume them.
- OOT output recognition accepts stable native program keys. The NoSharpening
  path uses an established tone-mapped output and validates its frame, channel
  and mapping identity. The original prepass and network work remain available
  for a same-frame fallback.
- Shader work includes shared-memory ordering, guarded global-buffer routing,
  atomic-return control flow and native offset handling. Syncpoint callbacks
  run outside the manager lock, with cancellation and exception cleanup.

## Shader preparation and loading

- I've balanced shader-build notifications across success, cancellation and
  failure, and kept the remaining-build count in the existing status label.
  Detached jobs retain their inputs and join before their owner is destroyed.
- Cache migration, AMD session pipeline binaries, stale warm-file detection,
  worker cancellation, cache locks and driver checkpoints are retained. A second
  process cannot recover or truncate a live shader-build journal.
- NSO reads and decompression lengths are checked before use. Module sizing uses
  checked arithmetic and the furthest segment, including OOT's additional BSS.
  NPDM ACI0 metadata, KIP section reads and BLZ expansion bounds are checked too.
- Windows sparse-vector fault handling preserves writable pages, including pages
  committed concurrently. OOT's video-finalize command is registered explicitly.

## UI, audio and source maintenance

- Session speed limits and saved graphics choices are retained. Movie scaling
  keeps Automatic, Bilinear and Game choices. Frame generation remains off by
  default, and every existing graphics control remains available.
- Audio keeps the microphone, None and silent-input choices, six-channel PCM and
  bounded host-queue refill. Empty saved input choices survive serialization.
- Adventure Notes keeps bounded script output, timeout recovery, missing-state
  handling and progress corrections. Per-game recommendations can be remembered,
  skipped or reset independently of global choices.
- I've restored our TGM banner in the main README and added nightly patch-release
  checks for source cleanliness, content provenance, matching packages, complete
  test receipts and checksums.
- I've replaced four game-derived shader fixtures with small, independently
  assembled programs. Their generators declare every instruction and environment
  value. Regression coverage includes byte stores, unmapped atomic counters,
  descriptor-alias routing, geometry passthrough and cache replay/migration.

Extract the whole Windows ZIP before running `lichii-tgm.exe`. On Linux, make the
AppImage executable and run it. The downloads include the project licence;
`SHA256SUMS`, the release manifest and test receipts identify the exact builds.

[All source changes since v1.0.3](https://github.com/ThatGuyMcd/Lichii-TGM/compare/Lichii-tgm-v1.0.3...Lichii-tgm-v1.0.4).

Thanks to the Lichii, Fritchii, Eglantine, Eden and yuzu contributors. Their original
copyright notices and licences remain. Games, keys and firmware are not included.
