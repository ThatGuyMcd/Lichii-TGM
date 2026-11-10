# Fritchii OOT comparison for v1.0.1

Snapshot: 2026-11-10, release `fritchii-v5.5` at
`4012e5271e278b21cf8f56b97481dac2b777cd0d`, and all 40 open/closed PRs returned
by the repository at review time. This is a compatibility review, not a claim that
combining branches has been benchmarked or that every experimental change is a win.

The release history was surveyed by commit, with actual diffs examined for the
candidate correctness fixes. All PR descriptions and changed-file sets were surveyed;
the applicable small patches and renderer candidates received a code comparison.
Old PRs against `nx2` can include hundreds of unrelated fork changes; their focused
replacement PRs are used instead of importing those whole branches.

## Selected release differences

| Source | Decision and evidence |
| --- | --- |
| `f52631cf66` | Port ASTC and unswizzle image-store bounds checks. Our shaders lacked them; Fritchii reports OOT AMD corruption/device faults. Both platform builds compile the changed shaders; hardware validation remains outstanding. |
| `9b30eec8ca`, rollback of upstream #820 | Adapt the reported NVIDIA fog regression: keep native interpolation on NVIDIA and retain the AMD workaround. Producer/consumer SPIR-V tests cover both paths. Do not blanket-revert the AMD fix. |
| `8b11f2500b` | Fritchii's async-translation architecture differs. Review found a related diagnostic edge here: pass the existing reference SPIR-V to the inline path after an IR-budget refusal. Default diagnostic mode stays off. |
| `15f8e4c5c` | Port the missing post-copy barrier hunk: later transfers must wait for downloads/gathers too. The corresponding CopyBuffer barrier was already covered by upstream #805. This is vendor-neutral ordering, not a measured FPS gain. |
| `a04eb9a800` | Port the late-request rejection missing from the upstream shutdown merge: do not enqueue captured callbacks after shutdown has released their waiters. |
| `763f3abb`, `5c173ca274` | Port the final unconditional inline-QMD register mirror. Production MME front/back tests expose the old mismatch in mapped-launch cases and check the control/unmapped boundaries. |
| `464fbb45bb` | The allocator already retries over-budget buffers. Do not duplicate its fallback or import its catch-all staging retry, which also catches device-loss exceptions. |
| `b3396c01f9` | Address-based guards protect Fritchii's EXP43 relocation. This fork has no such relocation architecture, so the patch is inapplicable. |
| `834026ac81`, `1c63b2b268` | Fritchii corrects its AMD shader-library default. This fork already defaults graphics pipeline libraries off; preserve users' explicit choices. |
| `9ea66c8273` | Foreign NVIDIA-to-AMD prepared-key conversion is a future cache-portability candidate. Our prepared-cache format and compatibility checks differ. Keep conservative rejection until an equivalent rekey proof and tests exist. |
| `c2d2f50d15`, rollback of #700 | Do not port. Its range rounding can omit the bytes actually written; our regression coverage catches that. Fritchii's justification depends on its different write/invalidation experiments. |
| UI, release tuning, process driver profiles, experimental defaults | Keep TGM identity and current supported features. No imported NVIDIA driver-profile writes, CPU-specific release tuning, or bulk experimental defaults. |

## Pull-request dispositions

Links refer to [Frizti/Fritchii pull requests](https://github.com/Frizti/Fritchii/pulls).
"Covered" means equivalent behaviour is present, often from the original Lichii PR;
it does not require importing a second copy or the fork's commit ancestry.

| PR | Decision |
| --- | --- |
| #1 | Fritchii-only flash-watch include order; that component is absent here. |
| #2 | Covered: query-cache flush before macro DMA reads, including split ownership. |
| #3 | Covered: SHF register-pressure operands and aligned uniform-shadow retry. |
| #4 | Controller/rumble work remains deferred as requested; includes protected dependency changes. |
| #5 | Covered: user modifications take precedence even on prepatched built-in records; existing patch tests retained. |
| #6 | Controller work remains deferred; changes SDL/dependency build wiring. |
| #7 | Fritchii-only draw-reuse probe declaration; this fork builds without that component. |
| #8 | Older bundle: microphone, fragment component masks, foliage/query waits and shutdown are covered by the individual upstream integrations. Do not merge the old-base bulk diff. |
| #9 | Broad Zen 3 tuning branch with no test description; not a portable or demonstrated OOT improvement for this fork. |
| #10 | Targets Fritchii's separate host-pacer architecture. A new presentation strategy requires separate pacing validation. |
| #11 | Covered: wait for pending query reports before split/non-split macro DMA reads. |
| #12 | Withdrawn, no patch. |
| #13 | Targets Fritchii's separate host-pacer architecture; not a drop-in Linux pacing fix here. |
| #14 | Mario Kart World-only opt-in preflush; outside OOT scope. |
| #15 | Covered: Linux topology-aware thread pinning and background-worker CPU masks. |
| #16 | Covered: Switch 2 microphone input and silent fallback; #31 targets the newer Fritchii base. |
| #17 | Old Fritchii Linux build fixes; corresponding conflicts do not block this fork's builds. |
| #18 | Same build fixes plus AMD driver-hang documentation, not a new rendering correction. |
| #19 | Fritchii portability fixes; current fork already builds on Linux with its own preparation implementation. |
| #20 | Covered: base-package metadata fallback and exact en-us NUP title priority. |
| #21 | Native Linux NGX DLSS already exists here, with separate runtime discovery and compatible settings. |
| #22 | Selected the bounded adaptive sysmix queue, with real 1,024-frame playback and capacity tests. Pinning, upstream movie/audio fixes and Linux builds are covered. Fritchii preparation/pacing and remaining optional tuning require separate integration proof. |
| #23 | Core fixes covered: valid history/motion state, image usage/barriers and scheduler-managed interpolation descriptors. Fritchii UI/counters differ. |
| #24 | Upstream #805 macro work and vector fence operations are covered. Remaining opt-in offloads/query batching lack an isolated OOT FPS gain in the PR; retain for a future measured experiment. |
| #25 | Older Linux portability alternative; no additional applicable fix over the current build. |
| #26 | Closed old-base compute branch; assess focused replacement #33. |
| #27 | Closed old-base render-target branch; assess focused replacement #34. |
| #28 | Closed old-base DMA branch; assess focused replacement #35. |
| #29 | Covered: fragment component masks in live pipelines and serialized prepared shader infos, including roundtrip tests. |
| #30 | Generic empty-value input/config serialization fix; controller work remains deferred. Not an OOT rendering or performance fix. |
| #31 | Covered: microphone service, validation, gain/conversion and unavailable-host silent capture. |
| #32 | Diagnostic bug report only, no emulator fix. |
| #33 | Covered: retain current compute pipeline while a pending rebuild compiles; retire queued/failed work safely. |
| #34 | Covered by upstream #1023: bounded transient render-target eviction under memory pressure. |
| #35 | Defer: skips query flushing based on an unverified GPU-page invariant and keeps an unbounded process-wide report-address map. Particularly unsuitable while investigating missing occlusion geometry. |
| #36 | Fritchii Graphite layout-specific controller UI correction; that UI is absent here. |
| #37 | Adapted relocatable AppRun and Qt 6 Wayland client deployment. Preserve the installed Qt version's actual plugin filenames. |
| #38 | Defer the bulk experimental DLSS-G/performance branch: requires RTX 40+, contains protected Dynarmic edits, and reports unresolved long-session device loss. The reported RTX 2080 SUPER cannot use its DLSS-G path. |
| #39 | Adapted launcher and explicit X11 preference. Build fixes are covered; do not import its Fritchii-specific shader-preparation default. |
| #40 | Covered by upstream #353 plus current prepared-info serialization; same result as #29. |

## Verification boundaries

The release manifest records final builds, test counts and package checks. No game
or emulator session was started or stopped during this review. Private logs, dumps,
captures and texture payloads are excluded. Runtime scene checks and FPS comparisons
remain outstanding; v1.0.1 does not claim to fix the NVIDIA kernel blue screen or
reconstruct damaged game files.
