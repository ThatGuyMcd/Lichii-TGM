# Lichii-tgm-v1.0.15

I've brought across another set of fixes and kept our Forest Lantern UI,
Horizon Couch Mode, settings and save locations intact.

- I've let Ocarina's existing No Sharpening option scale a smaller tonemap
  to the output size, including the 1440×810 handheld case. The original
  output runs when the image is unsuitable or the copy fails. Thanks to
  NoobOFN and Zortos. [Lichii #2611](https://github.com/zortos293/Lichii/pull/2611)
- I've made the texture cache fall back cleanly when a sampled view has
  no matching subresource. Thanks to MasonAlexandre.
  [Lichii #2608](https://github.com/zortos293/Lichii/pull/2608)
- I've fixed a shader compiler loop when a merge of shader values
  cannot be assigned a type. Thanks to nowhere02.
  [Lichii #2614](https://github.com/zortos293/Lichii/pull/2614)
- I've made Vulkan finish its fence worker before destroying the
  backend it uses. Thanks to Daniel Jesús Martín Flores.
  [Lichii #2619](https://github.com/zortos293/Lichii/pull/2619)
- I've tightened the checks around NVDRV and Host1x waits, display
  fences, video submissions and H.264 references. Invalid slots or
  IDs no longer modify the corresponding state, and video submits
  write only the fence thresholds covered by their requests. Thanks
  to Daniel Jesús Martín Flores.
  [Lichii #2623](https://github.com/zortos293/Lichii/pull/2623),
  [#2624](https://github.com/zortos293/Lichii/pull/2624),
  [#2628](https://github.com/zortos293/Lichii/pull/2628),
  [#2643](https://github.com/zortos293/Lichii/pull/2643)
- I've isolated the automated System and patch-manager test directories,
  so those tests can run together and leave each other's files alone.
  Thanks to Daniel Jesús Martín Flores.
  [Lichii #2620](https://github.com/zortos293/Lichii/pull/2620),
  [System test changes from #2071](https://github.com/zortos293/Lichii/pull/2071)

I've kept frame generation off by default. Existing per-game choices still apply.
The Windows ZIP and Linux AppImage come from the same source commit.

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
