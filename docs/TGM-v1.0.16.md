# Lichii-tgm-v1.0.16

I've brought across another set of fixes and kept our Forest Lantern UI,
Horizon Couch Mode, settings and save locations intact.

- I've fixed how input profiles with accents and other non-ASCII characters
  are saved. Their names now use the same encoding when I create or open
  them, so the same profile doesn't get listed twice. Thanks to Zortos.
  [Lichii #2662](https://github.com/zortos293/Lichii/pull/2662)
- I've corrected the Vulkan buffer flags used by post-processing effects.
  Thanks to Zortos. [Lichii #2657](https://github.com/zortos293/Lichii/pull/2657)
- I've added support for a previously unsupported 2D multisample texture
  instruction that reads its texture handle from a uniform register.
  Thanks to Zortos and capy-ai[bot].
  [Lichii #2653](https://github.com/zortos293/Lichii/pull/2653)
- I've made NVDEC and VIC shut down their command workers and retire their
  own outstanding fences before releasing device resources. Thanks to
  Daniel Jesús Martín Flores.
  [Lichii #2622](https://github.com/zortos293/Lichii/pull/2622)
- I've made Linux remove the directory entries for temporary decompressed
  NUP files while they're open, so an unexpected exit doesn't leave those
  files behind. Thanks to Zortos.
  [Linux fix from Lichii #2649](https://github.com/zortos293/Lichii/pull/2649)

I've kept frame generation off by default. Existing per-game choices still apply.
The Windows ZIP and Linux AppImage come from the same source commit.

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
