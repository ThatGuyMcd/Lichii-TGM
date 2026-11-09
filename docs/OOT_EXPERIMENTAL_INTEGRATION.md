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
