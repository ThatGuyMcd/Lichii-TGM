# v1.0.3 approved upstream integration

The owner approved the recommended merge list on 13 November 2026, with the Forest
Lantern UI and every setting retained and experimental changes left for later.
The integration starts at local `a75fda8a93274a1105a4f2862e5dc450f5ac2b7c`,
following fork nx2 `df55d4bd64894d6cf670c1a9188fe616edd8c6cd`.
Exact final source identity and validation belong in each package's build-info and
the release manifest.

## Reviewed snapshots

| Source | Snapshot |
| --- | --- |
| Lichii nx2 | `977dd8ab43a9bc0b375ff1b5ec003794b6aec1aa` |
| Fritchii nx2 | `f66ea633db6058efe31a9611718019e5bd1667eb` |
| Fritchii oot-gather-fix / v5.8.4 | `4f5c53149b7e17d1c24445f68d79a83a3b2a17f9` |

Main had 71 commits outside the fork's ancestry at review. Those include partial,
already-covered and intentionally excluded changes. Fritchii's default-only commit
is README text; its divergent release ancestry is not a count of newly missing fixes.
Metadata review covered 262 updated main PRs and all 66 Fritchii PRs, with actual
production comparisons for the selected subsets. Metadata review is distinct from
functional validation.

## Main selected changes

| PR | Source commit | Integrated scope |
| --- | --- | --- |
| #1384 | `2eb5f081c8` | Routine would-block log suppression |
| #1449 | `5afdffd134` | Disconnect stopped callback before forced Qt termination |
| #1450 | `1173323d43` | Nonblocking guest suspension before services power off |
| #1452 | `db7a53c1e2` | Mesh-emulation allocation retirement at the writing submission |
| #1453 | `4607cba98a` | Windows GUI/CLI crash context and local dumps, idempotent installation |
| #1456 | `e1320f64d7` | NX selected-program/platform agreement, retaining TGM NPDM lookup |
| #1457 | `24912830ef` | Missing-key diagnostics |
| #1458 | `c20441e60b` | Overflow-safe contained-range release and shared references |
| #1463 | `7281d70993` | `.zst` archive discovery; strict worker/cache contracts retained |
| #1465 | `c1ac940903` | Signed RTC/date and per-game state, retaining full-range numeric editing |
| #1466 | `d514ee0665` | Geometry interface, sparse-page and blit-layout checks |
| #1468 | `e84908679a` | Lazy SSA phis and identity cleanup; existing stack reuse retained |
| #1469 | `ddc658b874` | Versioned Switch 2 module names |
| #1485 | `bd0171f7b9` | Remaining shared compute-stage descriptor budgets |
| #1504 | `e43457be7c` | Self-contained ExeFS/RomFS identity, preserving mods and ordinary updates |
| #1505 | `09ce3bdc4e` | Interrupted cache boot recovery, preserving stale records and explicit-worker locks |
| #1515 | `5b9012f4b0` | Query batching/counters with TGM report ownership retained |
| #1516 | `342b839578` | Fence storage reuse, wake and semaphore subset; no compact-report replacement |
| #1521 | `45d0fb6807` | DMA/clear/inline uniform notifications and verifier consistency only |
| #1523 | `282a585aa8` | Pointer-word/null capture correction across runtime and native preparation |
| #1528 | `5613788172` | Same-size freed SMMU block reuse |
| #1532 | `0f8c5d503f` | Return recycled SMMU space on pressure and retry |
| #1536 | `fb93c19d8a` | Back up/apply/restore relevant per-game recommended overrides |
| #1475 draft | `ede29d400f` | Allocation-failure containment inside the actual noexcept formatter |

PR references use `https://github.com/zortos293/Lichii/pull/<number>`. Adaptations
preserve TGM's additional memory, motion, callback, recipe and cache guarantees.
They do not imply whole-PR or whole-tree equivalence.

## Fritchii selected subsets

| PR | Reviewed head | Integrated scope |
| --- | --- | --- |
| #44 | `2e87435472` | Hardware before software Vulkan devices |
| #49 | `d1869718ef` | Explicit UTF-8 frame-dump manifest/HTML handling |
| #60 | `35b99bfecb` | Explicit silent microphone in both capture APIs, output unaffected |
| #21 | `cae42d6483` | Versioned runtime discovery through the existing Linux NGX backend |
| #63 | `d4c80a9669` | Optional actual file times, bounded/error-aware driver-folder diagnostics |

PR references use `https://github.com/Frizti/Fritchii/pull/<number>`. The driver-folder
logger is a focused TGM implementation; Fritchii's original logger did not exist in
this fork. No foreign application IDs, data-path migration or backend replacement
is imported.

## Preserved and deferred

Existing local #1459 all-channel deletion cleanup and #1485 64-bit storage masks
remain. Main #1539 reverses a helper rewrite TGM never adopted; inline-capable
per-buffer branches and TGM's additional selection behavior remain. Existing CPU
write claiming, task-count initialization and dynamic-vertex safeguards are retained.

Protected externals, dependencies, src/dynarmic, RecompiledFuncs and
RecompiledPatches remain unchanged. Main's mass renames, copyright removal, foreign
themes/data paths and unrelated platform/network features are excluded.

Whole #1441/full-draw keeps, compact report callbacks, sparse reserve/kept-page
architecture, deferred page protection, asynchronous list copier, remaining computed
handle reuse and speculative read/discard policies remain separate work. No fixed
draw-drop timeout, mip reduction, hardware-specific pinning, unverified PGO profile
or new frame-generation backend/default is selected.

The source branch and prior artifacts preserve rollback points. All applicable
Windows/Linux tests, meaningful new regressions and package/retention checks must
pass on the exact release commit before push/tag/publication. No emulator/game is
started or stopped by this integration.
