# Lichii-tgm-v1.0.12

I've brought across fixes for shaders, texture handling, video playback, game
shutdown and the Album. I've kept our Forest Lantern UI, Horizon Couch Mode,
settings and save locations intact. Both downloads come from the same source commit.

- I've fixed a texture lifetime issue where a finished readback could free an
  image before later GPU work was done. Deleting an image also cancels its
  queued texture uploads so they can't touch a new image that takes its slot.
  Credit to Frizti, conongnang and Zortos. [Lichii #2485](https://github.com/zortos293/Lichii/pull/2485) · [Fritchii a308f9f](https://github.com/Frizti/Fritchii/commit/a308f9f4b2a21e6d2cf35d8fabc298d48a3acce7)
- I've stopped game shutdown from being started twice and blocked queued
  program switches while a game is closing. [Lichii #2502](https://github.com/zortos293/Lichii/pull/2502)
- Screenshots saved as PNG can now open in the Album at full size and as
  thumbnails. I've also fixed the decoded image being left in memory.
  [Lichii #2498](https://github.com/zortos293/Lichii/pull/2498)
- I've corrected the register reads for unpacked texture samples. That keeps
  mip bias and the shadow comparison value separate. I've also added a
  regression case for our existing 3D gradient guard and kept the gather
  tests covered with independently assembled inputs. [Lichii #2503](https://github.com/zortos293/Lichii/pull/2503) · [Lichii #2480](https://github.com/zortos293/Lichii/pull/2480)
- I've also replaced several old shader test inputs with independently
  assembled ones, keeping the coverage for uniform reads, carry and branches.
- I've initialised the texture upload cooldown, kept memory-pressure handling
  active when the OS reports zero available RAM, and added a Vulkan log
  message for hardware that stores Z24 depth as a float.
  [Lichii #2488](https://github.com/zortos293/Lichii/pull/2488) · [Lichii #2513](https://github.com/zortos293/Lichii/pull/2513) · [Lichii #2512](https://github.com/zortos293/Lichii/pull/2512)
- I've kept portable-directory and texture-worker tests reliable across
  directory aliases and worker scheduling. [Lichii #2505](https://github.com/zortos293/Lichii/pull/2505) · [Lichii #2504](https://github.com/zortos293/Lichii/pull/2504)
- I've kept shader compilation flags intact when the same shader is emitted
  twice, and fixed the signed addition overflow flag at the negative boundary.
  Credit to Daniel Jesús Martín Flores and RomainROCH.
  [Lichii #2523](https://github.com/zortos293/Lichii/pull/2523) · [Lichii #2525](https://github.com/zortos293/Lichii/pull/2525)
- I've fixed a NUP metadata edit that could overwrite a build ID stored in
  the publisher field. Other metadata can still be edited, and precache logs
  now show how long each file discovery step takes.
  [Lichii #2549](https://github.com/zortos293/Lichii/pull/2549) · [Lichii #2555](https://github.com/zortos293/Lichii/pull/2555)
- I've made the video input queue notify the game when it can accept more
  input, while keeping our existing queue limit.
  [Lichii #2529](https://github.com/zortos293/Lichii/pull/2529)
- I've corrected the Vulkan barrier after image reinterpretation so a
  following transfer can use the image.
  [Lichii #2561](https://github.com/zortos293/Lichii/pull/2561)
- I've replaced more of the shader test inputs with independently assembled
  programs, including the tessellation, shader-cache replay and geometry checks.
  Selected compatibility checks now use fingerprints of data from the
  player's own game files.

I've kept frame generation off by default. Existing per-game choices still apply.

Shader precompile system by Frizti (Fritchii). Shader precompile recipe by Frizti
(Fritchii), included with redistribution permission and its original notice.
