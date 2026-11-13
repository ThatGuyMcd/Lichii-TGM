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

Additional overlapping OOT branches are still being reconciled. Protected
dependencies and Dynarmic are not modified. The final integration audit will list
accepted, equivalent, superseded and blocked source changes individually.
