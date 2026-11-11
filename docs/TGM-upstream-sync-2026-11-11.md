# Upstream review for Lichii-tgm-v1.0.3

Reviewed on 11 November 2026 against published TGM v1.0.2,
`a30330c1e665e855919a743fee488491e7dfde19`.

## Source snapshots

| Repository / branch | Reviewed head | Scope |
| --- | --- | --- |
| [Lichii integrated nx2](https://github.com/zortos293/Lichii) | `5b09546918418589e26a304accabff21cc89db3b` | 36 commits after previously integrated `7dddff2d0e4b6f2802c12226c9d954771fe72113` |
| Lichii latest reviewed nx2 | `d5bb40ad4ca16b66e9c0961f823aff5f2a8c5c08` | Two later UI/LAN commits deferred |
| [Fritchii nx2](https://github.com/Frizti/Fritchii) | `f66ea633db6058efe31a9611718019e5bd1667eb` | Default branch unchanged |
| Fritchii oot-gather-fix / v5.8.4 | `4f5c53149b7e17d1c24445f68d79a83a3b2a17f9` | 28 commits after reviewed v5.7 `52317bf0e52a1ece5beb6474689d78c6f2f4894f` |
| [Eglantine nx2](https://github.com/Eglantine-Community/Eglantine) | `6be10b5c126a0faf88ffba00ff9ffeb9cd3f8c8e` | Divergent base; relevant fixes reviewed individually |

Open and closed PRs, relevant issues and unmerged branches were considered.
Reviewing a change does not imply importing it. This revision merges the reviewed
Lichii ancestry with the explicit source adaptations below; it does not claim
byte-for-byte equivalence with main or merge the entire Fritchii release branch.

## Main integration and retained behavior

The integration includes compiler corrections, read-only compute-buffer bounds,
empty-submit handling, Vulkan HDR presentation, bounded pipeline prebuilding,
retained SPIR-V and related core fixes. Rebuilt pipelines keep the exact cooked
module words and their GPL hashes/layouts. Retention failure remains optional;
failed insertion cannot leave incorrect byte accounting.

Two integration corrections have production regressions: pending reordered
uploads must prevent an empty-submit early return, and image fallback must use
the allocator's actual quarter-of-selected-VRAM policy. The latter preserves
retryable backing for temporary allocation failures and compressed light volumes.

The fork retains its Forest Lantern UI, settings, handheld overlay, portable/XDG/
Eden data locations and release packaging. New HDR and DLSS controls preserve old
saved IDs and choices. The default DLSS model remains game/NVIDIA selection.
Both Windows Streamline and Linux NGX use supported input ranges reported by the
runtime. Existing OOT aspects, Photo Mode, motion-vector information, fast-first
compilation, GPL builds and shader preparation remain integrated.

Protected dependency sources are unchanged. Specifically, Lichii's two Dynarmic
optimizations (`fd9c0133df`, `20621cda95`) and the external FidelityFX shader delta
are excluded. Tests that require those absent CPU optimizations are not enabled.
The upstream overlay redesign is also excluded in favor of the fork's existing UI.
The later dark-theme redesign (#1365) is reviewed and excluded: Forest Lantern
already provides the fork's touch spacing, scrolling and full-screen settings.
The original settings forms and apply paths remain the retention baseline.

The late-arriving archive update (#1232, `4d884cbb5b`) is included. nxformat 1.0.3
is linked statically with the existing zstd library; no separate nxformat DLL or
shared library is required. Native NX bases can use external updates and DLC;
RomFS dumping uses the selected patched content. Metadata edits release scanner
and provider sessions before writing. Whole-gamecard conversion preserves firmware
where supported. New regression archives are synthetic and their SHA-256 values
match the upstream fixture provenance; they contain no game data or keys.

## Focused correctness ports

| Source | Applied behavior |
| --- | --- |
| [Lichii #1311](https://github.com/zortos293/Lichii/pull/1311), `5d9a8f5a1281ff1ef56847ef20bf8c2b0a9d849b` | Emergency texture collection ignores diagnostic minimum age |
| [Lichii #1328](https://github.com/zortos293/Lichii/pull/1328), `555077432d4552e931b0b3825e851409bbd214f6` | Claim collected CPU writes before page downloads can overwrite later writes |
| [Lichii #1353](https://github.com/zortos293/Lichii/pull/1353), `fe2f322828339926ea6427aaa3a34f270268d440` | Contain allocation failure in optional zero-page trimming |
| Fritchii `60377e08049647a642ce455149979b39cf5bc357`, `5885850c68bc9346b4397bdc076ba7e8c020d957` | Correct high-register operand accounting while preserving richer TXD handling |
| [Fritchii #41](https://github.com/Frizti/Fritchii/pull/41), `1fbc5d222920ae250b7f9a1b43475f6f676ebe0a` | Native filesystem paths and explicit UTF-8 UI boundaries |
| [Fritchii #42](https://github.com/Frizti/Fritchii/pull/42), `af242e6ae7fdd5e9c9441165eaaee1bb8635c7f8` | Preserve installed mods when source and destination are equivalent |
| [Fritchii #48](https://github.com/Frizti/Fritchii/pull/48), `6d62f7e4304b61744903da25bbe62274b9971b6c` | Preserve occupied siblings and dangling links during mod rename |

Mod installation additionally rejects missing sources and overlapping parent/child
folders before deletion. Tests cover aliasing, Unicode, copy/move and cancellation.
OOT Texture Pool x2, ASTC policy/batching, pointer carry, replaced-download cleanup,
IMAD link accounting (Fritchii #51) and in-place blending (#52) were already covered.

## Reviewed changes left for separate validation

- Lichii #1285 learns eviction patterns by discarding the first read's GPU data.
  Its newer policy is not imported.
- Lichii #1218 changes registry callback ordering and lifetime; it needs additional
  concurrency and exception tests. #1314 changes texture identity/deletion.
- Lichii #1366 replaces recipes and starts preparation during gameplay. The fork
  keeps the explicit isolated worker, pinned metadata recipe, strict game identity,
  cache locks and genuine persisted-driver-cache checkpoints.
- Lichii #1306/#1336, high-refresh sound/water patches #1289/#1180, Fritchii #55
  mip removal/default tuning and Eglantine #824 require separate targeted evidence.
- DLSS-G changes are not included. Fritchii's Linux pinning/sampler alternatives,
  software-device ordering, archive editing/conversion and diagnostic-only changes
  are not treated as established OOT gameplay fixes.
- Fritchii v5.8.4 reverts its experimental lock-free read path; that experiment
  was never enabled in this fork.

Build, test, retention and package results belong in the accompanying build
manifest. Automated checks do not establish that location-dependent GPU resets
are fixed. Controlled gameplay validation remains outstanding.

## Daily follow-up at 20:34 UTC

The latest two main commits are reviewed without merging their ancestry:
`070e214d30dca550e5f487abf25f09ac9f9dece6` (#1371) changes the upstream dark
theme's row painter and icon/cover defaults; Forest Lantern retains its existing
layout and saved size behavior. `d5bb40ad4ca16b66e9c0961f823aff5f2a8c5c08`
(#1359) adds opt-in direct LAN transport, without two-PC or console validation.
It does not address the reported OOT failures. The fork remains two main commits
behind by ancestry at these compared tips; this is an explicit exclusion.

Further inspection of older unmerged shader PRs found narrow missing guards worth
adapting independently of their larger performance changes:

- [#1183](https://github.com/zortos293/Lichii/pull/1183),
  `0a1a56121b076aedc8ec5c0b785d136e013f3076`: reject the invalid lone-VertexB
  Position-input form during warm-up before it reaches the driver.
- [#1177](https://github.com/zortos293/Lichii/pull/1177),
  `49042f95d5ca10f59ffde9878a80d08409673841`: require the interpolation feature
  and sufficient bound descriptor sets before selecting graphics pipeline libraries.
- [#1188](https://github.com/zortos293/Lichii/pull/1188),
  `3df1a79e8c1b2a18a2d3fcd2d02607f99b06399b`: reduce automatic warm-up
  concurrency when no host commit headroom remains.

These are input/capability/memory safeguards, not proof that the owner's GPU reset
is fixed. The explicit recipe worker, cache format and existing shader settings
remain. Legacy warm-marker variant extensions are deferred for a separate cache
compatibility review; the explicit worker already has source/settings/driver stamps.
Position-input rejection applies to standalone library warm-up. Full recipe
preparation keeps its existing pipeline checks, and manual thread counts retain
precedence over automatic memory limits.

Other updated PRs were compared against production. Query-copy splitting (#1369),
persistent screenshot frames (#1374), and the useful input-device selection parts
of Fritchii #58 are covered. #1155's pool-wide descriptor flush is superseded by
the fork's descriptor-read path. Fritchii #59 reimports already-present NX support
and protected sources. #60's explicit no-microphone option remains a candidate for
stream lifetime, override and device validation. New asynchronous shader reuse/jobs
(#1378/#1379) require adaptation to OOT motion and GPL/monolithic translation inputs.
Apple/arm64 and MoltenVK-only changes remain outside the validated x86_64 target
scope, including partially covered LOD/null-buffer details; partial matches are not
reported as full ports. Fritchii release heads remain unchanged at v5.8.4.
