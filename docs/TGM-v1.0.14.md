# Lichii-tgm-v1.0.14

I've brought across another round of fixes while keeping our Forest
Lantern UI, Horizon Couch Mode, settings and save locations intact.

- I've restored the earlier texture overlap rules after upstream traced
  the Dodongo cutscene rendering regression to the newer mip-chain
  matching. Thanks to the upstream testers and Zortos.
  [Lichii #2578](https://github.com/zortos293/Lichii/pull/2578)
- I've moved texture-view generation allocation ahead of retirement.
  Ocarina's Vulkan effects still reject retired views, and a failed
  generation allocation rolls back the new slot. Thanks to RomainROCH
  and the upstream contributors.
  [Lichii #2573](https://github.com/zortos293/Lichii/pull/2573)
- I've reserved the slot free list when it grows so retiring a group of
  resources doesn't need more memory. Thanks to RomainROCH.
  [Lichii #2574](https://github.com/zortos293/Lichii/pull/2574)
- I've fixed the layout flag sent to Vulkan's 3D BCn texture decoder,
  so native-tiled blocks use the correct layout. Thanks to RomainROCH.
  [Lichii #2587](https://github.com/zortos293/Lichii/pull/2587)
- I've reduced unnecessary shader translation work for immediate
  register-to-predicate masks. Thanks to Reiixan.
  [Lichii #2577](https://github.com/zortos293/Lichii/pull/2577)
- I've fixed shader discard reordering so a discarded pixel stays
  discarded when the instruction is inside a loop or a block that
  can't safely be moved. Thanks to Daniel Jesús Martín Flores.
  [Lichii #2607](https://github.com/zortos293/Lichii/pull/2607)
- I've made shared fonts fall back to the existing built-in archive
  when the installed copy can't be read. Thanks to Zortos.
  [Lichii #2591](https://github.com/zortos293/Lichii/pull/2591)

I've kept frame generation off by default. Existing per-game choices still apply.
The Windows ZIP and Linux AppImage come from the same source commit.

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
