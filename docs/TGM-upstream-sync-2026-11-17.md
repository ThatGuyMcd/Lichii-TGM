# 17 November upstream integration

Baseline: `fdf8a8c3b10ce443bdda6aa577f1df7cf7d9e320`, retaining the locally
validated 16 November performance adapters. Rollback branch:
`codex/pre-upstream-sync-2026-11-17-0123`. Candidate version stays **1.0.4**.
This integration does not create a release or tag.

Reviewed snapshots:

* zortos293/Lichii nx2: `bfc2359f0ce6adc585f1c9f70a0d9b1e76ebf1d3`
* Frizti/Fritchii nx2: `f66ea633db6058efe31a9611718019e5bd1667eb`
* Frizti/Fritchii oot-gather-fix: `4f5c53149b7e17d1c24445f68d79a83a3b2a17f9`
* Djihads80/Eglantine nx2: `cf6cfe9caa6e160d3562742308a38eb3c9014789`

## Adapted components

* [#1995](https://github.com/zortos293/Lichii/pull/1995): correct the earlier
  #1875 NoSharpening copy. CB0+0xc0 describes the pre-tonemap HDR scene, rather
  than a valid tone-mapped copy source. Only a successfully recorded draw using
  one of the three established tone-mapping shader hashes establishes a source.
  Frame, channel and mapping epochs must match. Unknown draws, clears and
  DrawTexture invalidate the source; it can be consumed only once. The adapter
  deliberately omits the upstream size/order heuristic for unknown shaders.
  Original prepass/network dispatches are retained: a later invalidation or
  failed copy must still have valid network inputs in the same frame. A
  next-frame recovery latch cannot repair already skipped current-frame work.
* [#1918](https://github.com/zortos293/Lichii/pull/1918): reject short NSO reads
  and LZ4 output that does not match the declared segment length.
* [#1920](https://github.com/zortos293/Lichii/pull/1920): size NSO images from
  the furthest segment with checked 64-bit arithmetic. Also preflight stored
  extents, codec integer limits and argument capacity before allocation/copy.
  TGM's additional OOT BSS is counted in both sizing and loading passes; sizing
  still cannot apply or dump executable patches. Existing ZBIC compatibility
  behavior is preserved.
* [#1917](https://github.com/zortos293/Lichii/pull/1917): require ACI0 metadata
  magic while retaining both legacy and Switch 2 ACID signature layouts.
* [#1916](https://github.com/zortos293/Lichii/pull/1916): reject truncated KIP
  section reads.
* [#1879](https://github.com/zortos293/Lichii/pull/1879): bound BLZ footer/input
  indices and expansion arithmetic before allocation or backwards decoding.
* [#1881](https://github.com/zortos293/Lichii/pull/1881): preserve KIP BSS size
  without allocating a file-data buffer for BSS/unused virtual sections.
* [#2041](https://github.com/zortos293/Lichii/pull/2041): the Windows sparse
  vector read-fault path must leave pages writable, including pages another
  thread already committed. This avoids downgrading permissions during a race.
* [#1901](https://github.com/zortos293/Lichii/pull/1901): register OOT's video
  finalize command 903 explicitly, using the existing successful no-op handler.

New regressions exercise the actual NSO/NPDM/KIP parsers, actual Kepler dispatch
and copy-source contracts, tracker invalidation and the registered Windows
memory-fault handler. The existing full OOT module-layout fixture verifies the
additional BSS reservation and real loaded module boundaries.

## Deliberately deferred

Fritchii PR73 skips rendered work, so it cannot satisfy native 60 FPS. Raw mesh,
readback/overlap, prefault and image-pool proposals need additional correctness
or measured gameplay evidence. In particular, capped writeback provenance can
lose outstanding byte history, prefault jobs can outlive stack references on an
exception, and pooled images need device identity and non-allocating retirement.
New online services, foreign UI/defaults, hardware-specific policies and other
games' changes are not imported merely because their branches are newer.

The dated local audit in `build-local/upstream-watch/2026-11-17-0123` records
exact heads, actual production diffs and individual dispositions. Metadata
polling and semantic review are distinct: unreviewed proposals remain explicit
backlog, and historical audit registers are not claims of whole-PR equivalence.

## Validation and limits

Required gates are the exact committed Windows GUI/CLI/updater/worker and Linux
GUI/CLI/worker builds, all CTest groups, paired Windows ZIP/Linux AppImage,
protected-source and UI/settings retention, saved-profile hashes, version and
dependency checks, checksums, worker probes, unprivileged Linux cases and the
standalone AppImage Qt launcher. The dated validation receipt records actual
results; this document alone is not a pass certificate.

Every setting, Forest Lantern/handheld UI, data/save paths and OOT/aspect add-on
is retained. Frame generation remains off. No emulator session is launched or
stopped and no drivers, clocks or services are changed. Gameplay measurements
were explicitly deferred by the owner. Automated correctness checks do not
establish GPU image content, a twofold gain or native 60 FPS across the game.
