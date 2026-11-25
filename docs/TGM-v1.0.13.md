# Lichii-tgm-v1.0.13

I've brought across fixes for Ocarina's Vulkan effects, image transfers and
shader compilation. I've kept our Forest Lantern UI, Horizon Couch Mode,
settings and save locations intact.

- I've fixed Ocarina's Vulkan FSR effects pass so it can't reuse a retired
  scene view when a new view takes its cache slot at the same address.
  Thanks to nitramvi, conongnang, Frizti and Zortos.
  [Lichii #2566](https://github.com/zortos293/Lichii/pull/2566)
- I've corrected the Vulkan barrier after native image blits and resolves
  so later transfers can access the result. Thanks to RomainROCH.
  [Lichii #2563](https://github.com/zortos293/Lichii/pull/2563)
- I've fixed SPIR-V compilation when a shader's control flow selects a
  constant through an intermediate instruction that gets removed.
  Thanks to Mr-Tb and Kurebayashi.
  [Lichii #2567](https://github.com/zortos293/Lichii/pull/2567)
- I've made the graphics pipeline cache reject translations that depend on
  data the cache never recorded. The renderer can then build the pipeline
  from live data. Thanks to Zortos.
  [Lichii #2516](https://github.com/zortos293/Lichii/pull/2516)
- I've fixed how the texture cache matches a compatible render target to
  the base level of a mip chain. Interior views stay attached to the
  correct parent. Thanks to Zortos.
  [Lichii #2482](https://github.com/zortos293/Lichii/pull/2482)

I've kept frame generation off by default. Existing per-game choices still apply.
The Windows ZIP and Linux AppImage come from the same source commit.

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
