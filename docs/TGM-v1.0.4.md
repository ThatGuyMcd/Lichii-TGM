# Lichii-tgm-v1.0.4 integration candidate

This candidate combines compatible OOT Remake work from Lichii and Fritchii, including unmerged proposals and historical release branches. It is not a published release; v1.0.3 remains immutable. The [full audit](TGM-OOT-full-audit-2026-11-13.md) lists the captured candidates and deliberate exclusions.

- Renderer work includes command ownership, split-thread synchronization, draw/register reuse, tracked constant-buffer writes, sparse mapping and image invalidation, texture decoding/reuse and memory-pressure handling.
- Shader work includes owned detached translation, bounded job scheduling, cache reuse/migration, AMD session pipeline-binary retention, graphics libraries and native preparation, strict cache identity and stale warm-file rebuilding.
- Guarded OOT fixes cover moving platforms, water updates, sound-manager cadence, step counters, Gohma turn speed, optional text speeds and output/camera handling. Their code/state layout coexists with Photo Mode and all aspect add-ons.
- Optional upscalers and frame-generation backends retain explicit user choices and platform capability checks. OOT movie scaling gains Automatic, Bilinear and Game choices. Frame generation remains off by default.
- Audio retains microphone/None/silent input and six-channel PCM, with bounded host-queue refill. Empty saved input values survive serialization. Self-installing existing key files no longer invokes a throwing self-copy.
- Adventure Notes gains bounded script output, timeout recovery, safe missing-save/state handling and miniboss progress corrections. Per-game recommendations can be remembered, skipped or reset without overwriting global choices.

Forest Lantern dark/light, selected-game artwork, handheld layouts, full-screen settings, every original setting and existing data paths remain. New settings do not replace old controls or renumber saved enum values. Shader preparation retains its pinned recipe, explicit worker, cancellation, locks, checkpoints and strict game/build/GPU identity.

The optional Linux FSR4 runtime is built from unchanged pinned source, with licenses, corresponding source and provenance in the AppImage. API-load validation is separate from AMD rendering validation. Protected dependencies and Dynarmic sources are unchanged.

Exact source, package checksums and Windows/Linux validation results accompany the local artifacts. No native60, twofold performance improvement or crash cure is claimed by this integration. Further game-scene testing remains separate from automated validation.
