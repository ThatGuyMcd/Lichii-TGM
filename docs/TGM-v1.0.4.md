# Lichii-tgm-v1.0.4 integration candidate

The 15 November follow-up adapts [#1796](https://github.com/zortos293/Lichii/pull/1796)
to keep ASTC-to-BC staging buffers reserved through their final recorded GPU use.
Intermediate decode batches retain their compute barriers and submit together;
deferred output reservations are released even if command recording throws.
Actual GPU image-content and staging-reuse validation still requires a gameplay
or Vulkan-backed capture; unit tests alone do not establish that result.

[#1799](https://github.com/zortos293/Lichii/pull/1799) and
[#1800](https://github.com/zortos293/Lichii/pull/1800) supply balanced shader-build
notifications and the remaining-build count. The local adaptation also balances
constructor failures and unexpected build exceptions. Build owners join their
jobs before destroying the notification; completion is counted once. Actual
shader-job cancellation, failure and concurrent-completion regressions cover the
counter lifetime. The existing status label now shows how many builds remain,
without changing settings, forms, artwork or handheld layouts.

The native-performance test update adds linear mapping-span reuse for shifting
buffer bindings, avoids texture-cache locking for pages that never held images,
and gathers contended CPU writes before new or resumed command lists consume them.
Image pages are published before write tracking is armed. It also validates every
page of a requested guest span, preserves depthless native draws, and prevents
competing processes from recovering or truncating a live shader-build journal.

OOT's movie timeline and Scarecrow recording counters use fractional FPS-Unlock
ticks. Their relocated caves retain the existing Photo Mode, aspect and extra-BSS
layout. Exact base-game output signatures cover additional render resolutions.
Shader work adds cooperative workgroup-memory ordering and capability-gated NVIDIA
global-buffer routing, keeping guarded no-match reads and the existing atomic
fallbacks. Narrow QMD-alias, indirect-list lifetime and parser-shadow fixes retain
the current command ownership model. Syncpoint callbacks run outside the manager
lock, with cancellation, join and exception cleanup.

The Windows owner test profile has frame generation explicitly off. No resolution,
quality or other saved graphics choice is silently changed. Actual native FPS
and the remaining CPU/GPU bottleneck require repeated game-scene measurements;
this candidate does not claim to guarantee 60 FPS throughout the game.

The 16 November performance pass retires native upload provenance after dispatch, corrects
stream-buffer wrap ownership, and answers submapped-range queries from mapping runs. Deferred
staging buffers stay outside reuse scans, with allocation-free return during cleanup. Descriptor
capacity is prepared before draw/dispatch resource setup; multi-range retirement preserves
in-flight resources without submitting in the middle of setup. These adapt Lichii #1908,
#1835, #1820, #1906 and #1812 while retaining the local lifetime and invalidation guards.

OOT output recognition accepts stable native program keys as well as existing translated
variants (#1871). NoSharpening reads the image actually sampled by the output pass and clears
a stale skip latch when an output is missing (#1875). Healthy temporal DLSS still excludes
this bypass; the missing-output repair takes effect on the next prepass and does not replay
passes already skipped in the current frame. Texture collection preserves pending CPU writes
and publishes independent per-image landing guards (#1897). The GOB-order CPU swizzle proposal
(#1898) was evaluated but is not enabled: direct production-code comparisons found regressions
on larger layouts. The existing swizzle implementation is retained. Bit-exact layout tests and
CPU kernel measurements are separate from game FPS.

Reactive flushing, readback limits, output resolution, DLSS model policy and compiler defaults
are retained. Further changes to these policies require current gameplay measurements. The
candidate makes no claim of native 60 FPS everywhere; frame generation remains off for testing.

This candidate combines compatible OOT Remake work from Lichii and Fritchii, including unmerged proposals and historical release branches. It is not a published release; v1.0.3 remains immutable. The [full audit](TGM-OOT-full-audit-2026-11-13.md) lists the captured candidates and deliberate exclusions.

The [14 November follow-up](TGM-upstream-sync-2026-11-14.md) adds host-spill collection spacing,
session speed-setting retention, the load-end driver cache read guard, short-I/O
propagation and shared VFS reference lifetime. It also corrects a two-pass loader
reservation error that left the final SDK page unmapped after adding OOT patch
storage. An actual-loader regression reproduces the old error and passes with
the correction. The reported post-preparation guest abort still needs confirmation
in a game session.

- Renderer work includes command ownership, split-thread synchronization, draw/register reuse, tracked constant-buffer writes, sparse mapping and image invalidation, texture decoding/reuse and memory-pressure handling.
- Shader work includes owned detached translation, bounded job scheduling, cache reuse/migration, AMD session pipeline-binary retention, graphics libraries and native preparation, strict cache identity and stale warm-file rebuilding.
- Guarded OOT fixes cover moving platforms, water updates, sound-manager cadence, step counters, Gohma turn speed, optional text speeds and output/camera handling. Their code/state layout coexists with Photo Mode and all aspect add-ons.
- Optional upscalers and frame-generation backends retain explicit user choices and platform capability checks. OOT movie scaling gains Automatic, Bilinear and Game choices. Frame generation remains off by default.
- Audio retains microphone/None/silent input and six-channel PCM, with bounded host-queue refill. Empty saved input values survive serialization. Self-installing existing key files no longer invokes a throwing self-copy.
- Adventure Notes gains bounded script output, timeout recovery, safe missing-save/state handling and miniboss progress corrections. Per-game recommendations can be remembered, skipped or reset without overwriting global choices.

Forest Lantern dark/light, selected-game artwork, handheld layouts, full-screen settings, every original setting and existing data paths remain. New settings do not replace old controls or renumber saved enum values. Shader preparation retains its pinned recipe, explicit worker, cancellation, locks, checkpoints and strict game/build/GPU identity.

The optional Linux FSR4 runtime is built from unchanged pinned source, with licenses, corresponding source and provenance in the AppImage. API-load validation is separate from AMD rendering validation. Protected dependencies and Dynarmic sources are unchanged.

Exact source, package checksums and Windows/Linux validation results accompany the local artifacts. No native60, twofold performance improvement or crash cure is claimed by this integration. Further game-scene testing remains separate from automated validation.
