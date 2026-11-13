# Lichii-tgm-v1.0.4 integration candidate

This is an in-progress integration of the OOT Remake work reviewed from
zortos293/Lichii and Frizti/Fritchii. It is not a published release.

The first checkpoint includes renderer lifetime, cache scheduling and register
batching changes; OOT render-resolution and moving-platform corrections; memory
pressure and mapping fixes; shutdown cancellation; and six-channel audio support.
Regression tests cover the production paths and retained TGM behavior.

Forest Lantern, handheld layouts, every existing setting, data paths, aspect
ratios, Photo Mode and explicit shader preparation remain requirements throughout
integration. Frame generation is not enabled automatically. No native 60 FPS,
twofold performance improvement or crash cure is claimed by this candidate.

The second checkpoint adds shader reuse and binding caches, frame-generation
lifecycle/model choices and optional DLSS Frame Generation on Windows and Linux.
It preserves the prior LSFG Performance choice and keeps frame generation off by
default. DLSS Frame Generation needs compatible hardware and a separate runtime.

OOT water, sound and step-counter fixes use a dedicated appended state page to
coexist with Photo Mode and all aspect add-ons. Optional text speeds are available
in Add-Ons. The library stores custom artwork and names outside the game archive.
Linux NVIDIA cache defaults honor user overrides. Optional SDL trigger rumble
preserves the separate RenHDrum controller settings. New present-fence changes
include lifetime and bounded-queue regression coverage.

Additional overlapping OOT branches are still being reconciled. Protected
dependencies and Dynarmic are not modified. The final integration audit will list
accepted, equivalent, superseded and blocked source changes individually.
