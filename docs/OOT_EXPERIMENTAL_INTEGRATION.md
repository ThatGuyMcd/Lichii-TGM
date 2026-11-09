# Ocarina GPU and CPU integration manifest

Base: `986a2b684`. Integration branch: `oot-gpu-stack`.

## Authoritative implementations

- **#1028** is the readback implementation: asynchronous landing, pending-report retention,
  dirty writes, mirroring and scheduler-thread ownership remain intact.
- **#805** supplies GPU split command/write tracking, QMD and semaphore view, back macros,
  GPU-written command lists, ordered uploads and diagnostics. Its older readback code was
  reconciled into #1028 instead of replacing it.
- **#717** supplies CPU/JIT, kernel spin/wait, memory-trap, affinity and built-in Ocarina patch
  layers. Exact-code native macro handlers run through #805's register ownership and DMA path.
  Unsupported macro shapes fall back to the existing JIT before side effects. Occlusion macros
  retain real query reads; repeated native writes use the common dispatcher.
- **#559** supplies per-range query settlement, including completion when an unrun callback is
  destroyed. This is the stronger settlement requirement from #243, rather than #1032's queued
  reports alone. The front still conservatively flushes reports held by the query cache.

## Additional ports

| PR | Integrated behavior |
|---|---|
| #573 | AMD query availability resolve, wait on newest query per run, zeroed gaps; parallel texture decoding capped by spare physical cores |
| #802, #909 | Upload overlapping native uniform-bank bytes once per frame; skip that workaround for Ocarina except in verification mode |
| #874 | Keep payload-release fences synchronized at low accuracy |
| #933 | Poll suspended list-copy completion without a kernel call on every poll |
| #953 | Fast method profiling retained from #805; idle-hook work timed as flush work |
| #954 | Index-buffer resolution memo, disabled with accurate fences |
| #955 | Vertex-buffer bind memo scoped to the command-buffer epoch |
| #956 | #805 semaphore view supersedes queued-release prediction; met acquires retain a sync point |
| #974 | Vertex-ID, counter-reset, render-enable and report writes preserve eligible fast draws |
| #980 | GPU-written mesh counts remain on GPU, with register ownership and fallback tests |
| #235 | Optional `EDEN_NX2_SPLIT_SAFE_READS` queues safe-read flushes on the back |
| #615 | Clear state-tracker channel on release and teardown; remember bound channel |
| #869 | Full streamed-block zstd hook with checked module identity, alongside #717 call-site hooks |
| #861 | OpenGL compute binding locks and GPU wake helper; its competing preflush state machine is superseded by #1028 |
| #831 | Relevant JIT correctness and CPU work: atomic retry/abort handling, locked full barriers, FCMP, invalidation page index, CNTPCT, monitor waits, native guest CRC/Murmur/fmodf, FRECPE, host MXCSR, call/return, indirect branch cache, core-independent emitted code and emitter fixes |

## Superseded and remaining scope

The #345 + #861 preflush/bank-check combination reported to freeze in the guard room was not
recreated. #345's read synchronization is handled by #805/#1028/#559; #802/#909 handle bank upload
cost. #911's predictive fenced-release path is replaced by #805's semaphore view and actual
fence bookkeeping. #1032 is superseded by exact writer tracking and conservative report flushing.
Older #161/#166/#267 are umbrella predecessors: their overlapping GPU submission, split, JIT,
decoder and cache optimizations use the newer implementations. Their shader/pipeline/cache/UI
parts are the parent integration's responsibility, not blanket-merged here.

The standalone #717 front-state delta/repeated-draw batching and fake-query fast path were not
layered over #805. Native macro handlers use #805's dispatcher; the existing built-in Occlusion
Off patch remains available. #717's separate per-bind bank-shadow implementation was likewise
not layered over the later uniform reuse and bank-range code. These are explicit reconciliation
limits, not a claim that every line of each competing experimental stack is enabled.

## Validation and limits

Targeted Windows clang-cl 22 syntax checks used the configured parent build's definitions and
dependencies with this worktree's source. They cover GPU/split, DMA, Maxwell/Kepler/puller/draw,
Vulkan buffer/rasterizer/query, OpenGL rasterizer, texture decoder, native macros, core memory,
kernel/scheduler/arbiter/service, SVC/loader/zstd, ARM dynarmic and x64 emitter/interface/memory,
and buffer-port/mesh-argument/query-settlement/atomic-fallback/guest-function test translation
units. Full link and execution of tests are performed by the parent build. The new
`resolve_query_availability.comp` requires host shader regeneration.

No gameplay run, guard-room test, performance result or image comparison was performed locally.
Experimental kernel/TLS/JIT switches retain their upstream controls and need runtime validation
on the target machine. Existing reports mention Windows FAST_TLS boot trouble; this integration
has not independently reproduced or resolved that report. Disable `EDEN_NX2_FAST_TLS=0` for
triage if it occurs. Zstd hooks are build-ID and original-instruction guarded.

Attribution: merge/cherry-picks retain original authors. The manual #717 CPU port commit
`0f46b89` was mistakenly assigned to jpkto during integration; its source is Stephen Jayakar's
`eb3553bcb` / PR #717, and Stephen Jayakar is the correct author of those imported changes.


## Final shader stack (base cf3f95b9d, branch oot-shader-final)

| Source | Reconciled implementation |
|---|---|
| #589 | Safe interpreter fallback when macro JIT compilation rejects an unsupported program; fallback is forbidden after observable macro side effects. Live FIFO reads and interpreter side-effect tracking retained. |
| #300 | Software-HMMA shader metadata and a dedicated background compiler. Eligible dispatches skip until ready; native dispatches report deferred or failed compilation through the native shader error contract. AMD driver defaults and `EDEN_NX2_DEFER_MATRIX_PIPELINES` remain available. |
| #656 | A bounded 4096-entry shader reuse cache validates environment reads and all translation inputs. Detached native graphics jobs snapshot buffers, texture types and pointer classification. Snapshot misses fall back to current GPU-thread state; a later nonskippable draw joins the in-flight build. |
| #761 / #741 | Bounded archive-paired task/mesh predictions, reconstructed runtime state, target-format defaults, constant-table extraction, and a cancellable one-thread prewarm consumer. Transient descriptor pools and uncached speculative pipelines avoid filling the draw cache. Driver/device/settings/motion signatures and successful cache persistence gate completion markers. |
| #579 / #761 | Per-stage graphics pipeline libraries, native task/mesh library support, AMD fast-link correctness guard, monolithic fallback and optional optimized relinking. Motion-buffer descriptor sets and subgroup controls follow the actual library layout. |
| #579 / #741 | Optional fast-first graphics and compute pipelines (`EDEN_NX2_FAST_FIRST=1`) retain the first pipeline until its optimized replacement is ready; replacements bind in scheduler order. Shared large SPIR-V functions use DontInline controls consistently for translated and prepared modules. |

The #1021 recorded recipe and prepared Info/SPIR-V architecture remains authoritative. Its
prepared graphics modules use their recorded monolithic descriptor layout. Newly translated
pipelines can use libraries. This avoids treating monolithic prepared SPIR-V as per-stage-set
SPIR-V. #678 motion and depth context remain part of shader reuse and preparation signatures.
The serializer includes `uses_software_matrix`, and the preparation identity is `lichii-4`.

The newer bounded #998/#999 background prebuild queue remains in place rather than importing
#761's older refill implementation, which would retain unbounded captured environments in this
loader. Reload now cancels and drains translation, deferred compute, background prebuild and
mesh jobs before replacing cache state, then restores the capture/resident budget. #994 retention,
#995 failed-compute replacement ownership, heavy compilation limits and current recipe adoption
remain intact. An optimized compute build also prevents retirement until its worker completes.

The old general vertex/fragment prediction warm-up is represented by #1021's recorded recipe
preparation. Only the unique task/mesh prediction layer was added. There is no claim that every
old warm-up loader, job queue and on-disk format runs simultaneously. The in-app conversion guard
also prevents NUP conversion of a title whose shader preparation is using the same source.

Controls: graphics libraries require `EDEN_NX2_GPL=1`; optimized links additionally use
`EDEN_NX2_GPL_OPTIMIZE=1` where supported. Predicted mesh prewarm has the per-game
`use_mesh_shader_warmup` setting (default off). `shader_calls_not_inlined` defaults on, with
`EDEN_NX2_COMPUTE_NOINLINE` and `EDEN_NX2_GRAPHICS_NOINLINE` overrides. New recurring library,
worker-build and dispatch-wait timing diagnostics require `EDEN_NX2_GPU_PROFILE=1` or
`EDEN_NX2_STUTTER_TRACE=1`. The existing shader-dump and verification switches stay opt-in.

Older scope details: #166's native asynchronous snapshot, skip, wait and fallback behavior is
superseded by #656; its Switch 1 asynchronous extension is outside this Ocarina port. #161's
4096-draw / 5000-microsecond submission tuning is available through
`EDEN_NX2_FLUSH_DRAWS=4096` and `EDEN_NX2_FENCE_FLUSH_US=5000`, while the conservative defaults
remain 128 / 500. Deferred fragment barriers and immediate split list-copy submission are
already present. #267's descriptor-array, dual-vertex, texture and cache umbrella changes must
be accounted through their individual parent-integration descendants; it was not blanket merged
by this shader branch.

Attribution: the individual #589, #300, #656 and graphics-library commits preserve original
authors. The combined mesh port `209af8edf0` is based on Ewan Betts's `74f63c819f`, with later
#761 corrections, shader-archive constant extraction and PantACRO4life's warm-record/default
work. The final manual integration additionally adapts PantACRO4life's `6724498289` fast-first
work, DJ Lynch's `7869334bb3` shared-function control and later graphics control, and Ewan
Betts's `ae537ceaf7` reload/conversion/persistence review fixes.

Validation: targeted Windows clang-cl checks cover the new cache, graphics/compute pipelines,
library cache, shader reuse, warm builder/records/archive parser, Info serialization, macro
fallback and associated test sources. The standalone SPIR-V inline-control suite ran successfully:
5 test cases, 25 assertions. Parent integration performs the full link and broader test run.
No driver-specific rendering, OoT gameplay, warm-start performance or guard-room result is claimed.

## #831 native backend integration

The native backend branch starts at `0c3d77c370` and retains the individual upstream authors
and source commit IDs in 58 cherry-picks, followed by a combined compatibility fix. This is an
integration of the following native layers, not a claim that every implementation in #831 is
simultaneously active.

| Layer | Included behavior and reconciliation |
| --- | --- |
| Guest GPU addresses | GPU page tables, host-buffer page offsets, reverse mapping, once-per-frame null updates, storage/global-memory address lowering, and per-draw uniform-bank address tables. Address-backed draws take the full bind path so a previous draw's bank table cannot be reused accidentally. |
| Texture residency | Resident descriptor tables, watched device pages, image-specific invalidation, pool layout/version tracking, changed-entry queues and sampler class keys. The existing image recycler and low-memory allocation fallbacks remain authoritative. |
| Descriptor heaps | Capability-gated resource/sampler heaps, cached shadow descriptors, null entries, physical-page updates, per-stage mappings, GPL and shader-object draws. Pipelines with motion-history descriptors, unsupported images or nonzero OoT FSR LOD bias use the existing descriptor path. Heap pipeline binary capture is disabled to avoid conflicting creation-flags chains. |
| Shader objects | Shared `VK_EXT_shader_object` stage sets with dynamic rendering and state packets. Raw reusable SPIR-V is retained for verification; shader objects apply the same driver no-inline transform as shader modules. Recorded #1021 modules retain their monolithic descriptor layout. |
| Draw state | Exact draw-input watches and GPU-written-page generation, batched dynamic-state deltas, alpha/blend updates and additional dynamic raster state. OoT jitter is included when viewport packets are rebuilt. The AMD floating-point logic-op workaround changes only host state. |
| Pipeline libraries | Concurrent pre-raster/fragment builds, AMD quad-after-demote correctness guard, optimized background relinking and static vertex inputs masked by actual shader reads. Existing #656/#995 build ownership and scheduler-ordered retirement replace the competing optimized-handle implementation. |
| Driver resources | Vulkan memory priorities and pageable memory; driver pipeline-binary persistence for supported non-heap graphics/compute pipelines, including prepared/reused pipelines. |

Native storage and uniforms require buffer device addresses and their shader capabilities;
`EDEN_NX2_NATIVE_STORAGE=0` and `EDEN_NX2_NATIVE_UNIFORMS=0` disable the respective paths.
Descriptor heaps require `EDEN_NX2_DESCRIPTOR_HEAP=1`; shader objects require
`EDEN_NX2_SHADER_OBJECT=1`. These switches cannot enable a missing driver feature.
The original comparison controls are retained: `EDEN_NX2_DYNAMIC_RASTER=0`,
`EDEN_NX2_DYNSTATE_BATCH=0`, `EDEN_NX2_EXP_VERTEX_INPUT=dynamic`,
`EDEN_NX2_EXP_NO_GPL_RELINK=1`, `EDEN_NX2_EXP_NO_GPL=1`, and
`EDEN_NX2_EXP_NO_PIPELINE_BINARY=1`. Native comparison and draw-class diagnostics remain
off unless their `EDEN_NX2_NATIVE_VERIFY` / `EDEN_NX2_DRAW_CLASSES` controls are set.

The 112-byte common push-constant layout preserves the OoT motion area at offset 80, rescaling
data and render area. Heap-stage mappings start after that layout. Info serialization includes
the native address and quad-after-demote fields. Prepared-cache signatures include the layout
size, address capabilities, heap capability and shader-object mode; shader reuse also separates
these contexts. The parent `lichii-4` identity remains unchanged because the expanded context
invalidates incompatible preparations.

The pinned Vulkan headers `v1.4.345` provide the new extension declarations. The bundled Sirit
`v1.0.7` remains supported and was used for syntax validation: this emitter uses ordinary SPIR-V
with Vulkan heap resource mappings. The source-Sirit alternative retains its pinned version and
the checked-in include/extension patches, so it is reproducible without requiring that alternative
for the Windows build.

Remaining architectural limits: the current #805/#1028 render-pass barriers are retained rather
than importing #831's competing deferred-pass barrier tracker. Its hot-page demotion change
`483f404f6` targets an absent alternative bank-shadow cache and is not applied. The existing
shader-read-mask canonicalization replaces #831's alternate pipeline-state watch; whole-cache
lookup memoization was not imported. The heap path does not yet implement the per-draw OoT FSR
sampler LOD-bias variant, so those draws deliberately retain ordinary descriptors. These are
specific exclusions, not evidence that those optimizations are equivalent on every workload.

Validation: Windows clang-cl syntax checks cover the emitter and Info serialization, page-table
consumers, Vulkan device/wrapper/allocator, texture/buffer cache, heap, shader-object, binary and
pipeline code, scheduler/state packets, shader reuse, warm builder and new test sources. The
standalone native suite passes 71 cases and 5,516,133 assertions. The baseline warm-prompt bug is
fixed so absent/header-only files do not prompt and the per-game mesh opt-in is honored; native
recipe fallback remains intact. The uniform-upload test's frame transition now clears the same
per-frame range set as production `TickFrame`. The final integration still requires the parent's
full build/test run and hardware validation; no emulator or game was launched for these checks.

The separately linked buffer-cache / mesh-warm regression harness also passes all 71 cases and
15,621 assertions, including both baseline failures and the native residency/bank-table cases.
The unchanged-binding page-table test now verifies that no redundant upload occurs and both
mapped runs retain their translations. Final review also corrected deferred stage-pool release
to occur after every stage has emitted, and heap draws now adopt completed optimized links in
scheduler order.
